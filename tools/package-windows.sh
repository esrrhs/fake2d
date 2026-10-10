#!/usr/bin/env bash
#
# Assemble a self-contained Fake2D distribution folder on Windows, run from
# an MSYS2 MINGW64 shell (the same environment CI builds in).
#
#   package-windows.sh <binary.exe> <fl-bin-dir> <stage-root> [extra-asset ...]
#
# <fl-bin-dir> holds libfakelua.dll (the fakelua install "bin" directory).
# Every MinGW-runtime DLL the executable needs (libfakelua, OpenSSL,
# libwinpthread, libgcc/libstdc++) is copied next to it; Windows system DLLs
# and the software-GL Mesa override are intentionally left out so the host's
# real OpenGL32 is used.
set -euxo pipefail

BIN_IN="$1"; shift
FLBIN="$1"; shift
STAGE="$1"; shift

BIN_DIR="$STAGE/bin"
mkdir -p "$BIN_DIR" "$STAGE/scripts" "$STAGE/assets"

BIN="$BIN_DIR/fake2d_hello.exe"
cp -f "$BIN_IN" "$BIN"

cp -R scripts/. "$STAGE/scripts/"
cp -f assets/*.vert assets/*.frag "$STAGE/assets/" 2>/dev/null || true
for asset in "$@"; do
    [ -e "$asset" ] && cp -f "$asset" "$STAGE/assets/"
done

# Seed libfakelua.dll, then pull every MinGW-provided dependency to a
# fixed point. System32/Windows DLLs are never copied.
cp -f "$FLBIN/libfakelua.dll" "$BIN_DIR/"

mingw_dep() {
    # ldd lines look like:  libfoo.dll => /mingw64/bin/libfoo.dll (0x...)
    ldd "$1" 2>/dev/null | awk '/=>/ {print $3}' | while read -r p; do
        case "$p" in
            /mingw32/*|/mingw64/*|/usr/*)
                case "$(basename "$p")" in
                    opengl32.dll|gdi32.dll|d3d*.dll|dxgi.dll) ;;
                    *) echo "$p" ;;
                esac
                ;;
            *) ;;
        esac
    done
}

for _ in 1 2 3 4; do
    for f in "$BIN_DIR"/*.dll "$BIN"; do
        while read -r p; do
            [ -n "$p" ] && [ -f "$p" ] && cp -fu "$p" "$BIN_DIR/" || true
        done < <(mingw_dep "$f")
    done
done

# Anything the fakelua install bin itself provides that the loader resolves
# there (in case ldd prints a Windows-style path we did not match above).
for f in "$BIN_DIR"/*.dll "$BIN"; do
    ldd "$f" 2>/dev/null | awk '/=>/ {print $3}' | while read -r p; do
        case "$(cygpath -u "$p" 2>/dev/null || echo "$p")" in
            "$(cygpath -u "$FLBIN")"/*) cp -fu "$p" "$BIN_DIR/" || true ;;
        esac
    done
done

cat > "$STAGE/run.bat" <<'EOF'
@echo off
cd /d "%~dp0"
bin\fake2d_hello.exe %*
EOF

cp -f README.md LICENSE "$STAGE/" 2>/dev/null || true

echo "Packaged $STAGE"
ls "$BIN_DIR"
