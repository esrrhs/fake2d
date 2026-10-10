#!/usr/bin/env bash
#
# Assemble a self-contained Fake2D distribution folder on Linux.
#
#   package-linux.sh <binary> <fakelua-lib-dir> <stage-root> [extra-asset ...]
#
# Bundles libfakelua plus its OpenSSL runtime (resolved from `ldd`, so this
# works across distro sonames), re-points everything at $ORIGIN/../lib with
# patchelf, and emits a run.sh launcher. System libraries (glibc, GL/X11,
# ld-linux) stay provided by the host.
set -euxo pipefail

BIN_IN="$1"; shift
FLLIB="$1"; shift
STAGE="$1"; shift

command -v patchelf >/dev/null || {
    echo "patchelf is required (apt-get install patchelf)" >&2
    exit 1
}

BIN_DIR="$STAGE/bin"
LIB_DIR="$STAGE/lib"
mkdir -p "$BIN_DIR" "$LIB_DIR" "$STAGE/scripts" "$STAGE/assets"

BIN="$BIN_DIR/fake2d_hello"
cp -f "$BIN_IN" "$BIN"
chmod u+w "$BIN"

# Make sure ldd can resolve libfakelua regardless of the build's RUNPATH.
export LD_LIBRARY_PATH="$FLLIB:${LD_LIBRARY_PATH:-}"

cp -R scripts/. "$STAGE/scripts/"
cp -f assets/*.vert assets/*.frag "$STAGE/assets/" 2>/dev/null || true
for asset in "$@"; do
    [ -e "$asset" ] && cp -f "$asset" "$STAGE/assets/"
done

# Copy the libfakelua the executable actually resolves, as a real file named
# by its SONAME (avoid globbing in both the .so.2 symlink and .so.2.0.0).
FL_LINE=$(ldd "$BIN" | awk '/libfakelua\.so/ {print $1, $3; exit}')
FL_SONAME=$(echo "$FL_LINE" | awk '{print $1}')
FL_REAL=$(echo "$FL_LINE" | awk '{print $2}')
[ -n "$FL_SONAME" ] && [ -f "$FL_REAL" ]
cp -Lf "$FL_REAL" "$LIB_DIR/$FL_SONAME"
chmod u+w "$LIB_DIR/$FL_SONAME"

# OpenSSL is installed as a system package but is not present on every target
# distro, so bundle the exact libssl/libcrypto the build resolved to.
for soname in libssl.so libcrypto.so; do
    for f in "$BIN" "$LIB_DIR"/*.so*; do
        path=$(ldd "$f" 2>/dev/null | awk -v p="$soname" '$1 ~ ("^" p "\\.") {print $3; exit}')
        if [ -n "$path" ] && [ -f "$path" ]; then
            cp -Lf "$path" "$LIB_DIR/$(basename "$path")"
            chmod u+w "$LIB_DIR/$(basename "$path")"
        fi
    done
done

# Anything else living outside the standard system locations (a fakelua
# installed under /usr/local, for example) is bundled too.
for f in "$BIN" "$LIB_DIR"/*.so*; do
    ldd "$f" 2>/dev/null | awk '/=>/ {print $1, $3}' | while read -r name path; do
        [ -f "$path" ] || continue
        case "$path" in
            /lib/*|/lib64/*|/usr/lib/*|/usr/lib64/*) ;;
            *)
                cp -Lf "$path" "$LIB_DIR/$(basename "$path")" 2>/dev/null || true
                ;;
        esac
    done
done

# Rewrite search paths: the exe looks in ../lib, bundled libs look beside
# themselves.
patchelf --set-rpath '$ORIGIN/../lib' "$BIN"
for f in "$LIB_DIR"/*.so*; do
    chmod u+w "$f"
    patchelf --set-rpath '$ORIGIN' "$f"
done

cat > "$STAGE/run.sh" <<'EOF'
#!/bin/bash
cd "$(dirname "$0")"
export LD_LIBRARY_PATH="$(pwd)/lib:${LD_LIBRARY_PATH:-}"
exec ./bin/fake2d_hello "$@"
EOF
chmod +x "$STAGE/run.sh"

cp -f README.md LICENSE "$STAGE/" 2>/dev/null || true

echo "Packaged $STAGE"
if ldd "$BIN" | grep -q 'not found'; then
    echo "ERROR: unresolved dependencies remain:" >&2
    ldd "$BIN" | grep 'not found' >&2
    exit 1
fi
ldd "$BIN" | grep -E 'fakelua|ssl|crypto' || true
