#!/usr/bin/env bash
# Linux/GCC full library measurement; existing release/cross-platform gates
# remain independent and unchanged. CMAKE_PREFIX_PATH must locate FakeLua.
set -euo pipefail
cd "$(dirname "$0")/.."
build=${COVERAGE_BUILD_DIR:-build-coverage}
out=${COVERAGE_OUTPUT:-reports/coverage/current}
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DFAKE2D_COVERAGE=ON -DFAKE2D_WITH_FAKELUA=ON -DFAKE2D_BUILD_TESTS=ON "$@"
cmake --build "$build" --parallel "${COVERAGE_JOBS:-2}"
# Only this coverage build's counters; never touch another writer's build.
python3 - "$build" <<'PYCODE'
from pathlib import Path
import sys
for p in Path(sys.argv[1]).rglob('*.gcda'): p.unlink()
PYCODE
mkdir -p "$out"
build=$(realpath "$build")
out=$(realpath "$out")
# Publish a truthful partial report even when a suite/script fails. Preserve
# the suite failure; coverage can never turn a failing test run green.
finish() {
  status=$?
  trap - EXIT
  set +e
  python3 tools/coverage_report.py --build-dir "$build" --output "$out" --check
  report_status=$?
  if [ "$status" -ne 0 ]; then exit "$status"; fi
  exit "$report_status"
}
trap finish EXIT
"$build/bin/fake2d_hello" --generate-assets > "$out/assets.log" 2>&1
ctest --test-dir "$build" --output-on-failure --output-log "$out/ctest.log" -j 1
python3 tools/run_demo_scripts.py --hello "$build/bin/fake2d_hello" --frames 400 --interp --ai > "$out/demos.log" 2>&1
python3 tools/check_ui_demos.py --hello "$build/bin/fake2d_hello" > "$out/dynamic-ui.log" 2>&1
python3 tools/run_gpu_smoke.py --hello "$build/bin/fake2d_hello" --profile extended > "$out/gpu.log" 2>&1
