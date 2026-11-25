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


from integration_cases import CORE_JOBS, EXTENDED_EXTRA
from integration_bridge import gpu_cases, run_cases


def run_jobs(hello, jobs, errors):
    rc = run_cases(hello, gpu_cases(hello, jobs))
    if rc:
        errors.append("native Google Test GPU cases failed (see individual assertions above)")


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
