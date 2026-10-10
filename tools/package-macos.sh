#!/usr/bin/env bash
#
# Assemble a self-contained Fake2D distribution folder on macOS.
#
#   package-macos.sh <binary> <stage-root> [extra-asset ...]
#
# <binary>       path to the built fake2d_hello executable
# <stage-root>   final package directory, e.g. fake2d-1.5.0-macos-arm64
# extra-asset    generated asset files to ship (mario.json, ...); optional
#
# The script discovers every non-system dylib the binary pulls in (libfakelua
# and its OpenSSL dependency) directly from `otool -L`, so it works whether
# the toolchain linked Homebrew openssl@3 or openssl@4. All references are
# rewritten to @executable_path/../lib and every Mach-O is ad-hoc signed.
# Real (de-referenced) files are used -- never symlinks -- so the archive is
# safe to unpack on filesystems that do not preserve them.
set -euxo pipefail

BIN_IN="$1"; shift
STAGE="$1"; shift

BIN_DIR="$STAGE/bin"
LIB_DIR="$STAGE/lib"
mkdir -p "$BIN_DIR" "$LIB_DIR" "$STAGE/scripts" "$STAGE/assets"

BIN="$BIN_DIR/fake2d_hello"
cp -f "$BIN_IN" "$BIN"
chmod u+w "$BIN"

# Copy the script tree and the shader sources that live in assets/.
cp -R scripts/. "$STAGE/scripts/"
cp -f assets/*.vert assets/*.frag "$STAGE/assets/" 2>/dev/null || true
# Pre-generated demo maps / media, when the caller provides them.
for asset in "$@"; do
    [ -e "$asset" ] && cp -f "$asset" "$STAGE/assets/"
done

# Seed with the libfakelua the executable links against (de-reference any
# symlink: we store the physical file under its soname basename).
FL_REF=$(otool -L "$BIN" | awk '/libfakelua/ {print $1; exit}')
[ -n "$FL_REF" ]
cp -Lf "$FL_REF" "$LIB_DIR/$(basename "$FL_REF")"
chmod u+w "$LIB_DIR/$(basename "$FL_REF")"

# Pull in every non-system transitive dependency (libssl -> libcrypto, ...).
# A fixed-point loop because copying one dylib can reveal further deps.
copy_non_system_deps() {
    local target="$1"
    otool -L "$target" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        case "$dep" in
            /usr/lib/*|/System/*|@*|'') ;;
            *)
                local base
                base="$(basename "$dep")"
                if [ ! -f "$LIB_DIR/$base" ]; then
                    cp -Lf "$dep" "$LIB_DIR/$base"
                    chmod u+w "$LIB_DIR/$base"
                fi
                ;;
        esac
    done
}
for _ in 1 2 3 4; do
    for f in "$LIB_DIR"/*.dylib; do copy_non_system_deps "$f"; done
    copy_non_system_deps "$BIN"
done

# Re-point every external reference to @executable_path/../lib and give each
# bundled dylib a matching id. libfakelua references OpenSSL through the opt
# prefix rather than the Cellar path, so both spellings are handled here
# because the rewrite is driven by the actual load commands.
rewrite_refs() {
    local target="$1"
    chmod u+w "$target"
    otool -L "$target" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        case "$dep" in
            /usr/lib/*|/System/*|@*|'') ;;
            *)
                local base
                base="$(basename "$dep")"
                install_name_tool -change "$dep" \
                    "@executable_path/../lib/$base" "$target"
                ;;
        esac
    done
}
for f in "$LIB_DIR"/*.dylib; do
    install_name_tool -id "@executable_path/../lib/$(basename "$f")" "$f"
    rewrite_refs "$f"
done
rewrite_refs "$BIN"

# Ad-hoc sign after mutating all load commands.
codesign --force --sign - "$LIB_DIR"/*.dylib "$BIN"

# Double-clickable launcher that runs from the package root (all in-engine
# paths are relative to the working directory).
cat > "$STAGE/run.command" <<'EOF'
#!/bin/bash
cd "$(dirname "$0")"
exec ./bin/fake2d_hello "$@"
EOF
chmod +x "$STAGE/run.command"

cp -f README.md LICENSE "$STAGE/" 2>/dev/null || true

echo "Packaged $STAGE"
otool -L "$BIN" | grep -vE '/usr/lib|/System' || true
