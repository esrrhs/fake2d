#!/usr/bin/env python3
"""Cross-platform runner for the fake2d headless gameplay fixtures.

Assembles head + game script + tail chunks (see generate_fixtures.py) and
drives each scenario through the flua CLI under every FakeLua backend:

    jit_type=0  TCC JIT
    jit_type=1  GCC JIT (the engine default)
    jit_type=2  interpreter

Pure Python 3 standard library, so it runs identically on Linux, macOS and
Windows runners. Exit code is non-zero if any case fails or crashes.

Examples:
    python3 tests/headless/run_headless.py \\
        --flua "$HOME/flbuild/install/bin/flua"
    python tests/headless/run_headless.py --flua C:/msys64/mingw64/bin/flua.exe
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_fixtures import write_fixtures  # noqa: E402

# scenario: name, demo script, fixed-step frames, extra environment vars
SCENARIOS = [
    ("mario_smoke", "scripts/mario.lua", 200, {}),
    ("mario_death", "scripts/mario.lua", 300, {"MARIO_TEST_DEATH": "60"}),
    ("mario_win", "scripts/mario.lua", 360, {"MARIO_TEST_WIN": "60"}),
    ("mario_die3", "scripts/mario.lua", 400,
     {"MARIO_TEST_DIE3": "1", "MARIO_TEST_JUMP": "1"}),
    ("mario_phys", "scripts/mario.lua", 600, {"MARIO_TEST_PHYS": "1"}),
    ("mario_blocks", "scripts/mario.lua", 1800, {"MARIO_TEST_BLOCKS": "1"}),
    ("slope_smoke", "scripts/slope_demo.lua", 120, {}),
    ("platformer_smoke", "scripts/platformer_demo.lua", 120, {}),
]

# Script test hooks that must never leak between scenarios.
TEST_ENV_VARS = [
    "MARIO_TEST_DEATH", "MARIO_TEST_WIN", "MARIO_TEST_BIG", "MARIO_TEST_AI",
    "MARIO_TEST_RIGHT", "MARIO_TEST_JUMP", "MARIO_TEST_DIE3", "MARIO_TEST_TP",
    "MARIO_TEST_MUSH", "MARIO_TEST_PHYS", "MARIO_TEST_BLOCKS",
    "SLOPE_TEST_RIGHT", "SLOPE_TEST_JUMP", "SLOPE_TEST_SURFACE",
]


def resolve_flua(path: Path) -> Path:
    if path.is_file():
        return path
    candidate = path.with_name(path.name + ".exe")
    if candidate.is_file():
        return candidate
    raise FileNotFoundError(f"flua not found: {path} (also tried {candidate})")


def run_case(flua, chunk, jit_type, name, frames, extra_env):
    env = {k: v for k, v in os.environ.items() if k not in TEST_ENV_VARS}
    env.update(extra_env)
    env["FIX_SCENARIO"] = name
    env["FIX_FRAMES"] = str(frames)
    proc = subprocess.run(
        [str(flua), f"--jit_type={jit_type}", str(chunk)],
        env=env, capture_output=True, text=True, encoding="utf-8",
        errors="replace",
    )
    return proc.returncode, proc.stdout, proc.stderr


def main():
    repo_root = Path(__file__).resolve().parents[2]
    ap = argparse.ArgumentParser()
    ap.add_argument("--flua", type=Path, required=True,
                    help="path to the flua executable")
    ap.add_argument("--repo", type=Path, default=repo_root)
    ap.add_argument("--workdir", type=Path, default=None,
                    help="where generated chunks are written (default: temp)")
    ap.add_argument("--jit", default="0,1,2",
                    help="comma-separated FakeLua backends (default 0,1,2)")
    ap.add_argument("--scenario", default=None,
                    help="run only scenarios containing this substring")
    args = ap.parse_args()

    flua = resolve_flua(args.flua)
    jits = [int(x) for x in args.jit.split(",") if x]
    scenarios = [s for s in SCENARIOS
                 if args.scenario is None or args.scenario in s[0]]

    own_workdir = args.workdir is None
    workdir = Path(tempfile.mkdtemp(prefix="fake2d-headless-")) \
        if own_workdir else args.workdir
    workdir.mkdir(parents=True, exist_ok=True)
    write_fixtures(args.repo, workdir)
    head = (workdir / "m_head.lua").read_text()
    tail = (workdir / "m_tail.lua").read_text()

    chunk_paths = {}
    for _, script, _, _ in scenarios:
        if script not in chunk_paths:
            body = (args.repo / script).read_text()
            out = workdir / ("t_" + Path(script).name)
            out.write_text(head + body + tail, newline="\n")
            chunk_paths[script] = out

    total = 0
    failures = 0
    for jit in jits:
        print(f"===== flua --jit_type={jit} =====")
        for name, script, frames, extra_env in scenarios:
            total += 1
            rc, stdout, stderr = run_case(
                flua, chunk_paths[script], jit, name, frames, extra_env)
            # flua prints string results quoted: "PASS ..." / "FAILURES 0"
            marks = [ln.strip().strip('"') for ln in stdout.splitlines()
                     if ln.strip().strip('"').startswith(("PASS", "FAIL"))]
            summary = next((ln for ln in marks
                            if ln.startswith("FAILURES")), "")
            bad = [ln for ln in marks
                   if ln.startswith("FAIL ") or ln.startswith("FAIL\t")]
            case_failed = ("FAILURES 0" not in summary) or bool(bad)
            if case_failed:
                failures += 1
                print(f"BAD  {name} (rc={rc})")
                for ln in marks:
                    print("     " + ln)
                tail_err = "\n".join(stderr.splitlines()[-12:])
                if tail_err:
                    print(tail_err)
            else:
                print(f"OK   {name}")

    print(f"\n{total - failures}/{total} cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
