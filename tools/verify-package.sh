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
#   3. mario, snooker and contra render non-blank screenshots through the
#      package's own launcher, whose default --strict-assets makes a missing
#      texture an error rather than a quiet fall back to the procedural
#      painters;
#   4. removing a shipped texture makes that launcher exit non-zero and name
#      the missing file (the negative proof that the default is really strict);
#   5. the demo runs did not create any asset files (no first-run generation).
#
# Linux runners need xvfb; on macOS the package's CGL headless path is used.
# Windows packages only get the static check (no GL fixture here).
# The Build workflow runs this full proof on Linux only; macOS/Windows Build
# jobs use tools/verify-consumer.sh for the find_package consumer gate.
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

# Every shipped launcher must turn on strict loading. The runtime proof below
# can only drive the Unix launchers, so the Windows script is checked here.
for launcher in run.sh run.command run.bat; do
    if [ -f "$STAGE/$launcher" ]; then
        grep -F -- '--strict-assets' "$STAGE/$launcher" >/dev/null \
            || fail "$launcher does not pass --strict-assets"
    fi
done

# Runtime checks need a Unix launcher; Windows packages stop here.
if [ -x "$STAGE/run.sh" ]; then
    LAUNCHER="$STAGE/run.sh"
elif [ -x "$STAGE/run.command" ]; then
    LAUNCHER="$STAGE/run.command"
else
    say "no Unix launcher (Windows package); static check only"
    exit 0
fi

run_pkg() {
    if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
        xvfb-run -a -s '-screen 0 1024x768x24' \
            env LIBGL_ALWAYS_SOFTWARE=1 "$LAUNCHER" "$@"
    else
        env LIBGL_ALWAYS_SOFTWARE=1 "$LAUNCHER" "$@"
    fi
}

TMP=$(mktemp -d "${TMPDIR:-/tmp}/fake2d-verify.XXXXXX")
restore_neg() {
    if [ -f "$TMP/white.png" ]; then
        mv "$TMP/white.png" "$STAGE/assets/white.png"
    fi
}
trap 'restore_neg; rm -rf "$TMP"' EXIT
# Start somewhere unrelated: the launcher must relocate to the package root.
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

run_check ui --ui-test
grep -F '[ui] PASS' "$TMP/ui.log" >/dev/null || fail "UI layout test not PASS"
run_check ui-box-demo --headless --ui-demo --frames 3 \
    --screenshot "$TMP/ui.png" --shot-frame 2 --shot-min-colors 12
[ -s "$TMP/ui.png" ] || fail "UI box demo screenshot missing"
if grep -qF '[UI_OVERFLOW]' "$TMP/ui-box-demo.log"; then
    fail "UI box demo contains layout overflows"
fi

for demo in ui-layout ui-features ui-variable ui-stress; do
    run_check "$demo" --headless "--$demo-demo" --frames 240 \
        --screenshot "$TMP/$demo.png" --shot-frame 60 --shot-min-colors 12
    [ -s "$TMP/$demo.png" ] || fail "$demo screenshot missing"
    if grep -qF '[UI_OVERFLOW]' "$TMP/$demo.log"; then
        fail "$demo contains layout overflows"
    fi
done

run_check ui-stress --headless --ui-stress --frames 300 --ui-stress-seed 42
run_check ui-bench --headless --ui-bench
# The staged binary must be the 1.54 release and execute its portable complexity gate.
grep -F 'version=1.54.0' "$TMP/ui-bench.log" >/dev/null || fail "stale UI package version"
grep -F 'rich_batches_max=' "$TMP/ui-bench.log" >/dev/null || fail "missing rich batch counters"

run_check art --art-test
grep -F '[art] PASS' "$TMP/art.log" >/dev/null || fail "art test not PASS"

run_check mario --headless --ai --frames 180 \
    --mario-demo --screenshot "$TMP/mario.png" --shot-min-colors 12
[ -s "$TMP/mario.png" ] || fail "mario screenshot missing"

run_check snooker --headless --ai --frames 900 \
    --snooker-demo --screenshot "$TMP/snooker.png" --shot-frame 500 \
    --shot-min-colors 12
[ -s "$TMP/snooker.png" ] || fail "snooker screenshot missing"

run_check contra --headless --ai --frames 2500 \
    --contra-demo --screenshot "$TMP/contra.png" --shot-frame 1800 \
    --shot-min-colors 12
[ -s "$TMP/contra.png" ] || fail "contra screenshot missing"

# ---- 4. the launcher's default is strict -----------------------------------
# The runs above would pass even if the launcher omitted --strict-assets, since
# every asset ships. Hide one texture and require the launcher itself -- with
# no flag supplied here -- to fail and name the file instead of drawing a
# procedural substitute.
mv "$STAGE/assets/white.png" "$TMP/white.png"
if run_pkg --headless --ai --frames 120 --snooker-demo > "$TMP/strict.log" 2>&1; then
    cat "$TMP/strict.log"
    fail "the launcher ran without strict loading; a missing texture went unnoticed"
fi
grep -F 'texture load failed: assets/white.png' "$TMP/strict.log" >/dev/null || {
    cat "$TMP/strict.log"
    fail "a missing texture did not report 'texture load failed'"
}
restore_neg
say "launcher fails on a missing texture"

AFTER=$(snapshot)
if [ "$BEFORE" != "$AFTER" ]; then
    diff <(printf '%s\n' "$BEFORE") <(printf '%s\n' "$AFTER") >&2 || true
    fail "the package generated asset files at run time (procedural fallback)"
fi
say "no assets generated at run time"
say "PASS"
