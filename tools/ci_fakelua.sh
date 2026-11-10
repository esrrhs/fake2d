#!/usr/bin/env bash
#
# Shared fakelua install helper for the Build workflow.
#
#   tools/ci_fakelua.sh identity   # write FAKELUA_CACHE_KEY to GITHUB_ENV
#   tools/ci_fakelua.sh install    # build+install when the prefix is missing
#   tools/ci_fakelua.sh verify     # require a usable prefix (bin + cmake package)
#
# The cache key includes runner image, arch, compiler, OpenSSL and FAKELUA_REF
# plus the install flags (Release, tests/benchmarks off) so a stale ABI cannot
# be reused. FLINSTALL must already point at the prefix.
set -euo pipefail

cmd=${1:?usage: tools/ci_fakelua.sh identity|install|verify}

flag_id="rel-nt-nb-v1"

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

verify_prefix() {
    local prefix=${FLINSTALL:?FLINSTALL is not set}
    [ -n "$(cmake_config "$prefix")" ] && flua_bin "$prefix" >/dev/null
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
