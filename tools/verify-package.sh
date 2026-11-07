#!/usr/bin/env bash
#
# Verify a staged Fake2D package ships everything its demos load and still runs
# after being moved to a different working directory.
#
#   tools/verify-package.sh <stage-root>
#
# Run from the repository (CI does); the script locates the root from its own
# path. Checks:
#   1. every file tracked by git under assets/ is present in the package, plus
#      the first-run artifacts the host normally generates (maps, sheets,
#      clips, language packs);
#   2. --art-test passes in the package, started from an unrelated directory;
#   3. mario, snooker and contra render non-blank screenshots with
#      --strict-assets, so a missing texture is an error rather than a quiet
#      fall back to the procedural painters;
#   4. the demo runs did not create any asset files (no first-run generation).
#
# Linux runners need xvfb; on macOS the package's CGL headless path is used.
# Windows packages only get the static check (no GL fixture here).
set -euo pipefail

STAGE_ARG=${1:?usage: verify-package.sh <stage-root>}
REPO=$(cd "$(dirname "$0")/.." && pwd)
STAGE=$(cd "$STAGE_ARG" && pwd)

say() { printf 'verify-package: %s\n' "$*"; }
fail() { printf 'verify-package: ERROR: %s\n' "$*" >&2; exit 1; }

[ -d "$STAGE/assets" ] || fail "package has no assets/ directory: $STAGE"

# ---- 1. static completeness ------------------------------------------------
missing=0
while IFS= read -r rel; do
    [ -s "$STAGE/$rel" ] || {
        printf 'verify-package: missing %s\n' "$rel" >&2
        missing=1
    }
done < <(git -C "$REPO" ls-files -- assets)

# Generated at first run by the host (see --generate-assets); the package must
# ship them instead of relying on that fallback.
for rel in \
    assets/mario.json assets/platformer.json assets/slope.json \
    assets/level.json assets/path.json \
    assets/tiles.png assets/pf_tiles.png assets/_tile_anim.json \
    assets/_tile_anim.png assets/ta_level.json assets/ta_sheet.png \
    assets/white.png assets/ball.png assets/brick.png assets/paddle.png \
    assets/coin.png assets/music.wav assets/lang/en.tsv assets/lang/zh.tsv; do
    [ -s "$STAGE/$rel" ] || {
        printf 'verify-package: missing generated %s\n' "$rel" >&2
        missing=1
    }
done
[ "$missing" -eq 0 ] || fail "package is missing shipped files"
say "static asset tree complete"

# Runtime checks need the Unix launcher; Windows packages stop here.
if [ ! -x "$STAGE/run.sh" ]; then
    say "no run.sh (non-Unix package); static check only"
    exit 0
fi

run_pkg() {
    if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
        xvfb-run -a -s '-screen 0 1024x768x24' \
            env LIBGL_ALWAYS_SOFTWARE=1 "$STAGE/run.sh" "$@"
    else
        env LIBGL_ALWAYS_SOFTWARE=1 "$STAGE/run.sh" "$@"
    fi
}

TMP=$(mktemp -d "${TMPDIR:-/tmp}/fake2d-verify.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
# Start somewhere unrelated: run.sh must relocate to the package root itself.
cd "$TMP"

snapshot() { (cd "$STAGE/assets" && find . -type f -print | sort); }
BEFORE=$(snapshot)

run_check() {
    local name=$1
    shift
    if ! run_pkg "$@" > "$TMP/$name.log" 2>&1; then
        cat "$TMP/$name.log"
        fail "$name failed"
    fi
    if grep -qE 'texture load failed|strict assets:' "$TMP/$name.log"; then
        cat "$TMP/$name.log"
        fail "$name hit a missing texture"
    fi
    if grep -q 'update() failed' "$TMP/$name.log"; then
        cat "$TMP/$name.log"
        fail "$name threw in update()"
    fi
    say "$name ok"
}

run_check art --strict-assets --art-test
grep -F '[art] PASS' "$TMP/art.log" >/dev/null || fail "art test not PASS"

run_check mario --headless --strict-assets --ai --frames 180 \
    --mario-demo --screenshot "$TMP/mario.png" --shot-min-colors 12
[ -s "$TMP/mario.png" ] || fail "mario screenshot missing"

run_check snooker --headless --strict-assets --ai --frames 900 \
    --snooker-demo --screenshot "$TMP/snooker.png" --shot-frame 500 \
    --shot-min-colors 12
[ -s "$TMP/snooker.png" ] || fail "snooker screenshot missing"

run_check contra --headless --strict-assets --ai --frames 2500 \
    --contra-demo --screenshot "$TMP/contra.png" --shot-frame 1800 \
    --shot-min-colors 12
[ -s "$TMP/contra.png" ] || fail "contra screenshot missing"

AFTER=$(snapshot)
if [ "$BEFORE" != "$AFTER" ]; then
    diff <(printf '%s\n' "$BEFORE") <(printf '%s\n' "$AFTER") >&2 || true
    fail "the package generated asset files at run time (procedural fallback)"
fi
say "no assets generated at run time"
say "PASS"
