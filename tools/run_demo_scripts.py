#!/usr/bin/env python3
"""Run every catalog demo and fail on script compile/update errors.

A CompileFile failure still continues without a script and can exit 0, so
each log is scanned for `syntax error`, `CompileFile.*failed` and
`update() failed` in addition to the exit code. The inverse fixture
`scripts/update_fail_test.lua` is not a catalog demo and is not run here.

  tools/run_demo_scripts.py [--hello PATH] [--frames 400] [--interp] [--ai]
"""

from __future__ import annotations

import argparse
import sys

from ci_common import (
    add_hello_argument, die_with_errors, list_demos, resolve_hello, run_hello,
    script_error_hits,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    add_hello_argument(parser)
    parser.add_argument("--frames", type=int, default=400)
    parser.add_argument("--interp", action="store_true")
    parser.add_argument("--ai", action="store_true")
    args = parser.parse_args()
    hello = resolve_hello(args.hello)
    demos = list_demos()
    if not demos:
        return die_with_errors("run_demo_scripts", ["no demos found in demos.h"])
    errors: list[str] = []
    for demo in demos:
        cmd = ["--headless"]
        if args.interp:
            cmd.append("--interp")
        if args.ai:
            cmd.append("--ai")
        cmd += ["--frames", str(args.frames), f"--{demo}-demo"]
        print(f"run_demo_scripts: {' '.join(cmd)}", flush=True)
        rc, log = run_hello(hello, cmd)
        hits = script_error_hits(log)
        if rc != 0 or hits:
            reason = f"exit {rc}"
            if hits:
                reason += f", log: {', '.join(hits)}"
            errors.append(f"demo '{demo}' {reason}\n{log}")
            print(f"run_demo_scripts: FAIL {demo} ({reason})", file=sys.stderr, flush=True)
        else:
            print(f"run_demo_scripts: ok {demo}", flush=True)
    if not errors:
        print(f"run_demo_scripts: PASS ({len(demos)} demos, {args.frames} frames)")
    return die_with_errors("run_demo_scripts", errors)


if __name__ == "__main__":
    raise SystemExit(main())
