#!/usr/bin/env python3
"""Shared JIT/GL demo and benchmark smoke for the Build workflow.

`core` is the Linux mid-length smoke (long gameplay milestones stay in the
Linux screenshot-gates step). `extended` adds shorter JIT runs of the game
demos on macOS/Windows, which have no long milestone step.

Dynamic UI layout/features/variable/stress acceptance is
`tools/check_ui_demos.py`, not this script. Every command here also fails on
`syntax error` / `CompileFile.*failed` / `update() failed`, not just a zero
exit. The expected-failure fixture `scripts/update_fail_test.lua` is not run.

  tools/run_gpu_smoke.py [--hello PATH] [--profile core|extended]
"""

from __future__ import annotations

import argparse
from pathlib import Path

from ci_common import (
    ROOT, add_hello_argument, die_with_errors, resolve_hello, run_hello,
    script_error_hits,
)


def job(label: str, args: list[str], screenshot: str | None = None,
        contains: list[str] | None = None,
        forbids: list[str] | None = None) -> dict:
    return {
        "label": label,
        "args": args,
        "screenshot": screenshot,
        "contains": contains or [],
        "forbids": forbids or [],
    }


# Mid-length JIT/GL smoke shared by all three OS jobs.
CORE_JOBS = [
    job("scene-30", ["--headless", "--frames", "30", "--scene-demo"]),
    job("scene-shot", ["--headless", "--frames", "40", "--scene-demo",
                       "--screenshot", "scene.png"],
        screenshot="scene.png"),
    job("text-shot", ["--headless", "--frames", "60", "--text-demo",
                      "--screenshot", "text.png", "--shot-min-colors", "12"],
        screenshot="text.png"),
    job("bench", ["--headless", "--bench"]),
    job("tile", ["--headless", "--frames", "120", "--tile-demo"]),
    job("map", ["--headless", "--frames", "120", "--map-demo"]),
    job("platformer", ["--headless", "--ai", "--frames", "180", "--platformer-demo"]),
    job("mario", ["--headless", "--ai", "--frames", "180", "--mario-demo"]),
    job("slope", ["--headless", "--ai", "--frames", "120", "--slope-demo"]),
    job("path", ["--headless", "--frames", "120", "--path-demo"]),
    job("rt", ["--headless", "--frames", "120", "--rt-demo"]),
    job("ui", ["--headless", "--frames", "120", "--ui-demo"]),
    job("ui2", ["--headless", "--frames", "120", "--ui2-demo"]),
    job("ui3", ["--headless", "--frames", "120", "--ui3-demo"]),
    job("post", ["--headless", "--frames", "120", "--post-demo"]),
    job("fx", ["--headless", "--frames", "120", "--fx-demo"]),
    job("fog", ["--headless", "--frames", "120", "--fog-demo"]),
    job("audio", ["--headless", "--frames", "120", "--audio-demo"]),
    job("spatial", ["--headless", "--frames", "120", "--spatial-demo"]),
    job("phys-bench", ["--headless", "--phys-bench"]),
    job("phys-lua", ["--headless", "--fixed-sim-dt", "--frames", "200",
                     "--entry", "scripts/phys_test.lua"],
        contains=["[script] 0"],
        forbids=[r"\[script] (10[0-9][0-9]|200[0-9])"]),
]

# macOS/Windows do not run the long Linux milestone gates; keep a JIT compile
# of those game scripts here at modest length. Attract-mode milestones stay
# Linux-only.
EXTENDED_EXTRA = [
    job("angry-birds", ["--headless", "--frames", "120", "--angry-birds-demo"]),
    job("limbo", ["--headless", "--frames", "120", "--limbo-demo"]),
    job("skeleton", ["--headless", "--frames", "120", "--skeleton-demo"]),
    job("light", ["--headless", "--frames", "120", "--light-demo"]),
    job("light-bench", ["--light-bench"], contains=["[light-bench] PASS"]),
    job("beat-em-up", ["--headless", "--ai", "--frames", "180", "--beat-em-up-demo"]),
    job("shmup", ["--headless", "--ai", "--frames", "180", "--shmup-demo"]),
    job("snooker", ["--headless", "--ai", "--frames", "180", "--snooker-demo"]),
    job("contra", ["--headless", "--ai", "--frames", "180", "--contra-demo"]),
    job("city", ["--headless", "--ai", "--frames", "180", "--city-demo"]),
    job("rts", ["--headless", "--ai", "--frames", "180", "--rts-demo"]),
    job("vn", ["--headless", "--ai", "--frames", "180", "--vn-demo"]),
]


def run_jobs(hello: Path, jobs: list[dict], errors: list[str]) -> None:
    import re
    for item in jobs:
        label = item["label"]
        print(f"run_gpu_smoke: {label}: {' '.join(item['args'])}", flush=True)
        rc, log = run_hello(hello, item["args"])
        hits = script_error_hits(log)
        if rc != 0 or hits:
            reason = f"exit {rc}"
            if hits:
                reason += f", log: {', '.join(hits)}"
            errors.append(f"{label}: {reason}\n{log}")
            print(f"run_gpu_smoke: FAIL {label} ({reason})", flush=True)
            continue
        shot = item["screenshot"]
        if shot:
            path = Path(shot) if Path(shot).is_absolute() else ROOT / shot
            if not path.is_file() or path.stat().st_size == 0:
                errors.append(f"{label}: missing or empty screenshot {path}")
                print(f"run_gpu_smoke: FAIL {label} (empty screenshot)", flush=True)
                continue
        failed_marker = False
        for needle in item["contains"]:
            if needle not in log:
                errors.append(f"{label}: missing expected output {needle!r}\n{log}")
                failed_marker = True
        for pattern in item["forbids"]:
            if re.search(pattern, log):
                errors.append(f"{label}: forbidden pattern {pattern!r} in log\n{log}")
                failed_marker = True
        if failed_marker:
            print(f"run_gpu_smoke: FAIL {label} (log markers)", flush=True)
        else:
            print(f"run_gpu_smoke: ok {label}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    add_hello_argument(parser)
    parser.add_argument("--profile", choices=("core", "extended"), default="core")
    args = parser.parse_args()
    hello = resolve_hello(args.hello)
    jobs = list(CORE_JOBS)
    if args.profile == "extended":
        jobs.extend(EXTENDED_EXTRA)
    errors: list[str] = []
    run_jobs(hello, jobs, errors)
    if not errors:
        print(f"run_gpu_smoke: PASS ({args.profile}, {len(jobs)} jobs)")
    return die_with_errors("run_gpu_smoke", errors)


if __name__ == "__main__":
    raise SystemExit(main())
