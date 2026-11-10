#!/usr/bin/env bash
#
# Shared fakelua install helper for the Build workflow.
#
#   tools/ci_fakelua.sh identity   # write FAKELUA_CACHE_KEY to GITHUB_ENV
#   tools/ci_fakelua.sh install    # build+install when the prefix is missing
#   tools/ci_fakelua.sh verify     # require a usable prefix (bin + cmake + TCC)
#
# The cache key includes runner image, arch, compiler, OpenSSL and FAKELUA_REF
# plus the install flags (Release, tests/benchmarks off) so a stale ABI cannot
# be reused. FLINSTALL must already point at the prefix.
#
# libtcc bakes CONFIG_TCCDIR to $FAKELUA_BUILD/third_party/tinycc/lib/tcc at
# configure time. cmake --install does not copy that tree into the prefix, so a
# cache hit on a fresh runner would lose stdarg.h (TCC's builtin headers are
# not in /usr/include). install stores the tree under $FLINSTALL/lib/tcc and
# rematerializes it at the baked path before the prefix is reused.
set -euo pipefail

cmd=${1:?usage: tools/ci_fakelua.sh identity|install|verify}

flag_id="rel-nt-nb-v2"

compiler_id() {
    local cxx=${CXX:-}
    if [ -z "$cxx" ]; then
        if command -v c++ >/dev/null 2>&1; then
            cxx=c++
        elif command -v g++ >/dev/null 2>&1; then
            cxx=g++
        elif command -v clang++ >/dev/null 2>&1; then
            cxx=clang++
        else
            echo "ci_fakelua: no C++ compiler on PATH" >&2
            exit 1
        fi
    fi
    printf '%s-%s' "$("$cxx" -dumpmachine)" "$("$cxx" -dumpversion)"
}

openssl_id() {
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists openssl; then
        pkg-config --modversion openssl
    else
        printf 'none'
    fi
}

cache_key() {
    local image=${ImageOS:-unknownos}-${ImageVersion:-unknownver}
    local arch=${RUNNER_ARCH:-$(uname -m)}
    local ref=${FAKELUA_REF:?FAKELUA_REF is not set}
    printf 'fakelua-%s-%s-%s-openssl%s-%s-%s' \
        "$image" "$arch" "$(compiler_id)" "$(openssl_id)" "$ref" "$flag_id"
}

cmake_config() {
    find "$1" -name fakeluaConfig.cmake 2>/dev/null | head -n 1
}

flua_bin() {
    local prefix=$1
    if [ -x "$prefix/bin/flua" ]; then
        printf '%s' "$prefix/bin/flua"
    elif [ -f "$prefix/bin/flua.exe" ]; then
        printf '%s' "$prefix/bin/flua.exe"
    else
        return 1
    fi
}

libfakelua() {
    local prefix=$1
    local f
    for f in \
        "$prefix/lib/libfakelua.so" \
        "$prefix/lib/libfakelua.dylib" \
        "$prefix/bin/libfakelua.dll" \
        "$prefix/lib/libfakelua.dll"
    do
        if [ -f "$f" ]; then
            printf '%s' "$f"
            return 0
        fi
    done
    f=$(ls "$prefix"/lib/libfakelua.so* "$prefix"/lib/libfakelua.*.dylib 2>/dev/null | head -n 1 || true)
    if [ -n "$f" ] && [ -f "$f" ]; then
        printf '%s' "$f"
        return 0
    fi
    return 1
}

tcc_runtime_ok() {
    [ -f "$1/lib/tcc/include/stdarg.h" ]
}

# TinyCC's own `make install` prefix (CONFIG_TCCDIR is $dir/lib/tcc... wait,
# CONFIG_TCCDIR *is* this directory: .../third_party/tinycc/lib/tcc).
build_tccdir() {
    local build=${1:-${FAKELUA_BUILD:-}}
    local d
    [ -n "$build" ] || return 1
    for d in "$build/third_party/tinycc/lib/tcc" "$build/third_party/tinycc"; do
        if [ -f "$d/include/stdarg.h" ]; then
            printf '%s' "$d"
            return 0
        fi
    done
    return 1
}

# Copy TinyCC's CONFIG_TCCDIR tree into the cached prefix and record the baked
# absolute path so a later runner can put the files back where libtcc looks.
install_tcc_runtime() {
    local prefix=${1:?}
    local src
    if ! src=$(build_tccdir "${FAKELUA_BUILD:-}"); then
        echo "ci_fakelua: TinyCC runtime headers missing under ${FAKELUA_BUILD:-unset}" >&2
        echo "ci_fakelua: expected \$FAKELUA_BUILD/third_party/tinycc/lib/tcc/include/stdarg.h" >&2
        return 1
    fi
    mkdir -p "$prefix/lib/tcc"
    cp -a "$src"/. "$prefix/lib/tcc/"
    printf '%s\n' "$src" > "$prefix/lib/tcc/.baked-tccdir"
    echo "ci_fakelua: stored TinyCC runtime from $src"
}

# libtcc searches {CONFIG_TCCDIR}/include, which is an absolute build-tree path.
# Recreate that directory (and the current FAKELUA_BUILD layout) from the
# cached copy inside the prefix.
materialize_tccdir() {
    local prefix=${1:?}
    local cached=$prefix/lib/tcc
    local dests=() dest seen=""

    [ -f "$cached/include/stdarg.h" ] || return 1

    if [ -f "$cached/.baked-tccdir" ]; then
        dests+=("$(tr -d '\r\n' < "$cached/.baked-tccdir")")
    fi
    if [ -n "${FAKELUA_BUILD:-}" ]; then
        dests+=("$FAKELUA_BUILD/third_party/tinycc/lib/tcc")
    fi

    for dest in "${dests[@]}"; do
        [ -n "$dest" ] || continue
        case " $seen " in
            *" $dest "*) continue ;;
        esac
        seen+=" $dest"
        if [ "$dest" = "$cached" ]; then
            continue
        fi
        # -ef is true when dest already is the cached tree (same inode).
        if [ -d "$dest" ] && [ "$dest" -ef "$cached" ]; then
            continue
        fi
        mkdir -p "$dest"
        cp -a "$cached"/. "$dest/"
        echo "ci_fakelua: rematerialized TinyCC runtime at $dest"
    done

    if [ -f "$cached/.baked-tccdir" ]; then
        dest=$(tr -d '\r\n' < "$cached/.baked-tccdir")
        if [ ! -f "$dest/include/stdarg.h" ]; then
            echo "ci_fakelua: failed to rematerialize baked CONFIG_TCCDIR $dest" >&2
            return 1
        fi
    elif [ -n "${FAKELUA_BUILD:-}" ] && [ ! -f "$FAKELUA_BUILD/third_party/tinycc/lib/tcc/include/stdarg.h" ]; then
        echo "ci_fakelua: failed to rematerialize \$FAKELUA_BUILD TinyCC dir" >&2
        return 1
    fi
}

# Compile a tiny main package with JIT_TCC. INTERP also compiles TCC (pcall /
# load closures), so this is the gate that must pass after a cache hit.
probe_tcc() {
    local prefix=${1:?}
    local flua err
    flua=$(flua_bin "$prefix") || return 1
    local tmp
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/ci-fakelua-tcc.XXXXXX")
    cat > "$tmp/probe.lua" << 'LUA'
package "main"
function init()
    return 0
end
function update()
    return 0
end
function fini()
    return 0
end
LUA
    export LD_LIBRARY_PATH="$prefix/lib:${LD_LIBRARY_PATH:-}"
    export DYLD_LIBRARY_PATH="$prefix/lib:${DYLD_LIBRARY_PATH:-}"
    export PATH="$prefix/bin:${PATH:-}"
    set +e
    "$flua" --once --jit_type=0 "$tmp/probe.lua" >"$tmp/out" 2>"$tmp/err"
    local rc=$?
    set -e
    if [ "$rc" -ne 0 ]; then
        echo "ci_fakelua: TCC probe failed (rc=$rc)" >&2
        sed -n '1,40p' "$tmp/err" >&2 || true
        rm -rf "$tmp"
        return 1
    fi
    rm -rf "$tmp"
}

verify_prefix() {
    local prefix=${FLINSTALL:?FLINSTALL is not set}
    [ -n "$(cmake_config "$prefix")" ] && flua_bin "$prefix" >/dev/null || return 1
    tcc_runtime_ok "$prefix" || return 1
    materialize_tccdir "$prefix" || return 1
    probe_tcc "$prefix"
}

case "$cmd" in
    identity)
        key=$(cache_key)
        echo "ci_fakelua: cache key $key"
        if [ -n "${GITHUB_ENV:-}" ]; then
            echo "FAKELUA_CACHE_KEY=$key" >> "$GITHUB_ENV"
        fi
        ;;
    verify)
        if ! verify_prefix; then
            echo "ci_fakelua: invalid or missing install at ${FLINSTALL:-unset}" >&2
            exit 1
        fi
        echo "ci_fakelua: verified $(flua_bin "$FLINSTALL")"
        ;;
    install)
        prefix=${FLINSTALL:?FLINSTALL is not set}
        if verify_prefix; then
            echo "ci_fakelua: reusing install at $prefix"
            exit 0
        fi
        src=${FAKELUA_SRC:-${RUNNER_TEMP:-/tmp}/fakelua-src}
        build=${FAKELUA_BUILD:-${RUNNER_TEMP:-/tmp}/flbuild}
        mkdir -p "$prefix"
        if [ ! -d "$src/.git" ]; then
            rm -rf "$src"
            git clone --no-checkout https://github.com/esrrhs/fakelua "$src"
        fi
        git -C "$src" fetch --depth 1 origin "$FAKELUA_REF"
        git -C "$src" checkout FETCH_HEAD
        extra=()
        if [ -n "${OPENSSL_PREFIX:-}" ]; then
            extra+=(-DCMAKE_PREFIX_PATH="$OPENSSL_PREFIX")
        fi
        cmake -S "$src" -B "$build" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DFAKELUA_BUILD_TESTS=OFF \
            -DFAKELUA_BUILD_BENCHMARKS=OFF \
            -DCMAKE_INSTALL_PREFIX="$prefix" \
            "${extra[@]}"
        cmake --build "$build" --parallel
        cmake --install "$build"
        if ! install_tcc_runtime "$prefix"; then
            echo "ci_fakelua: install did not capture the TinyCC runtime" >&2
            exit 1
        fi
        if ! verify_prefix; then
            echo "ci_fakelua: install did not produce a usable prefix" >&2
            exit 1
        fi
        ccache -s || true
        echo "ci_fakelua: installed $prefix"
        ;;
    *)
        echo "usage: tools/ci_fakelua.sh identity|install|verify" >&2
        exit 2
        ;;
esac
