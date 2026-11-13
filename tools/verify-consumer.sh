#!/usr/bin/env bash
#
# Install fake2d from a build tree and compile a throwaway consumer that
# declares no C++ standard, then run it to confirm find_package(fake2d 1.54)
# and the 1.54 UI APIs work.
#
#   tools/verify-consumer.sh [--build-dir DIR] [--prefix DIR]
#
# CMAKE_PREFIX_PATH must include fakelua (the installed fake2d prefix is
# prepended by this script). This is the portable consumer gate run on
# Linux, macOS and Windows in the Build workflow.
#
# Full staged-package verification (tools/verify-package.sh) remains Linux
# on the Build workflow: it needs the Linux launcher, xvfb and the long
# asset/screenshot proof. The Release workflow still stages packages on
# all three OSes; Windows packages only get the static checks in
# verify-package.sh.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$REPO/build"
PREFIX=""
CONSUMER_SRC="$REPO/tools/consumer"

while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir) BUILD=$2; shift 2 ;;
        --prefix) PREFIX=$2; shift 2 ;;
        *) echo "usage: tools/verify-consumer.sh [--build-dir DIR] [--prefix DIR]" >&2; exit 2 ;;
    esac
done

# Allow a relative build dir from the caller's cwd or the repo root.
if [ ! -f "$BUILD/CMakeCache.txt" ] && [ -f "$REPO/$BUILD/CMakeCache.txt" ]; then
    BUILD="$REPO/$BUILD"
fi
if [ ! -f "$BUILD/CMakeCache.txt" ]; then
    echo "verify-consumer: $BUILD is not a CMake build directory" >&2
    exit 1
fi

if [ -z "$PREFIX" ]; then
    PREFIX="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/fake2d-prefix"
fi
CONSUMER="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/fake2d-consumer"
rm -rf "$PREFIX" "$CONSUMER"
mkdir -p "$PREFIX" "$CONSUMER"

echo "verify-consumer: installing to $PREFIX"
cmake --install "$BUILD" --prefix "$PREFIX" >/dev/null

if grep -q "CXX_STANDARD" "$CONSUMER_SRC/CMakeLists.txt"; then
    echo "verify-consumer: ERROR: the consumer must declare no C++ standard; that is the point" >&2
    exit 1
fi

TARGETS=$(find "$PREFIX" -name fake2dTargets.cmake | head -n 1)
if [ -z "$TARGETS" ]; then
    echo "verify-consumer: ERROR: fake2dTargets.cmake was not installed under $PREFIX" >&2
    exit 1
fi
grep -q "cxx_std_23" "$TARGETS" || {
    echo "verify-consumer: ERROR: $TARGETS does not declare cxx_std_23" >&2
    exit 1
}

# The consumer must find both fake2d and fakelua. Callers pass fakelua via
# CMAKE_PREFIX_PATH / FLINSTALL; the fake2d prefix always goes first.
PREFIX_PATH="$PREFIX"
if [ -n "${CMAKE_PREFIX_PATH:-}" ]; then
    PREFIX_PATH="$PREFIX;$CMAKE_PREFIX_PATH"
elif [ -n "${FLINSTALL:-}" ]; then
    PREFIX_PATH="$PREFIX;$FLINSTALL"
fi

GENERATOR=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR=(-G Ninja)
fi

cmake -S "$CONSUMER_SRC" -B "$CONSUMER/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$PREFIX_PATH" \
    "${GENERATOR[@]}"
cmake --build "$CONSUMER/build" -j 4

# Resolve the binary on POSIX and Windows.
BIN=""
for candidate in "$CONSUMER/build/consumer" "$CONSUMER/build/consumer.exe"; do
    if [ -f "$candidate" ]; then
        BIN=$candidate
        break
    fi
done
if [ -z "$BIN" ]; then
    echo "verify-consumer: ERROR: consumer binary was not built" >&2
    exit 1
fi

# fakelua / installed fake2d runtimes must be loadable.
if [ -n "${FLINSTALL:-}" ]; then
    if [ -d "$FLINSTALL/lib" ]; then
        export LD_LIBRARY_PATH="$FLINSTALL/lib:${LD_LIBRARY_PATH:-}"
        export DYLD_LIBRARY_PATH="$FLINSTALL/lib:${DYLD_LIBRARY_PATH:-}"
    fi
    if [ -d "$FLINSTALL/bin" ]; then
        export PATH="$FLINSTALL/bin:$PATH"
    fi
fi
if [ -d "$PREFIX/lib" ]; then
    export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
    export DYLD_LIBRARY_PATH="$PREFIX/lib:${DYLD_LIBRARY_PATH:-}"
fi
if [ -d "$PREFIX/bin" ]; then
    export PATH="$PREFIX/bin:$PATH"
fi

OUTPUT=$("$BIN")
echo "$OUTPUT"
echo "$OUTPUT" | grep -F "consumer: 1.54" >/dev/null || {
    echo "verify-consumer: ERROR: consumer did not print the 1.54 version line" >&2
    exit 1
}
echo "verify-consumer: PASS"
