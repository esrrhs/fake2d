#!/usr/bin/env python3
"""Acceptance gate for the 1.47-1.50 dynamic UI demos.

Used by Linux `tools/check-docs.py` and as a required Build step on macOS
(CGL) and Windows (llvmpipe). It is the portable source of the marker,
grow/shrink/scroll metric, overflow, and screenshot colour checks -- do not
re-implement a weaker copy in the workflow.

  tools/check_ui_demos.py [--hello PATH]
"""

from __future__ import annotations

import argparse
import re
import tempfile
from pathlib import Path

from ci_common import ROOT, add_hello_argument, die_with_errors, resolve_hello, run_hello, script_error_hits


UI_DEMO_FAILURE_MARKERS = ("update() failed", "[UI_OVERFLOW]", "strict assets:", "[ERROR]")
MIGRATED_CALLERS = (
    "audio", "beat-em-up", "fx", "map", "platformer", "post",
    "shmup", "snooker", "text", "tile", "ui", "ui2", "ui3",
)


def validate_ui_demos(hello: Path, errors: list[str] | None = None) -> list[str]:
    """Compile/run the real FakeLua scenarios; host logs update errors but
    historically CompileFile can still exit 0, so checking only the process
    status is insufficient."""
    if errors is None:
        errors = []
    hello = resolve_hello(hello)
    with tempfile.TemporaryDirectory(prefix="fake2d-ui-demos-") as directory:
        for flag, markers, shot in [("ui-layout", range(148000, 148006), 60),
                                    ("ui-features", range(148100, 148103), 100),
                                    ("ui-variable", range(149000, 149006), 60),
                                    ("ui-stress", range(149100, 149106), 60)]:
            image = Path(directory) / (flag + ".png")
            rc, log = run_hello(hello, [
                "--headless", "--strict-assets", "--frames", "240",
                f"--{flag}-demo", "--screenshot", str(image),
                "--shot-frame", str(shot), "--shot-min-colors", "12",
            ])
            if (rc or not image.is_file() or not image.stat().st_size
                    or script_error_hits(log)
                    or any(x in log for x in UI_DEMO_FAILURE_MARKERS)):
                errors.append(f"{flag}: dynamic UI demo failed (exit {rc})\n{log}")
            for marker in markers:
                if f"[script] {marker}" not in log:
                    errors.append(f"{flag}: deterministic transition {marker} did not run")
            if flag == "ui-layout":
                metrics = {int(stage): tuple(map(float, values)) for stage, *values in
                           re.findall(r"\[script\] 14801([0-5])\n\[script\] ([0-9.e+-]+)\n"
                                      r"\[script\] ([0-9.e+-]+)\n\[script\] ([0-9.e+-]+)", log)}
                if len(metrics) != 6:
                    errors.append("ui-layout: missing measured dynamic transition metrics")
                else:
                    for short, long, shrunk in [(0, 1, 2), (3, 4, 5)]:
                        if not (metrics[long][0] > metrics[short][0] and
                                abs(metrics[shrunk][0] - metrics[short][0]) < .01 and
                                metrics[long][1] > 106 and metrics[shrunk][1] < 106 and
                                metrics[shrunk][2] == 0):
                            errors.append(f"ui-layout: grow/shrink/scroll metrics failed: {metrics}")
            if flag in ("ui-variable", "ui-stress"):
                base = "14900" if flag == "ui-variable" else "14910"
                fields = r"\n\[script\] ([0-9.e+-]+)" * 4
                metrics = {int(stage): tuple(map(float, values)) for stage, *values in
                           re.findall(r"\[script\] " + base + r"([0-5])" + fields, log)}
                if len(metrics) != 6:
                    errors.append(f"{flag}: missing measured transition metrics")
                elif flag == "ui-variable":
                    if not (metrics[1][3] > metrics[0][3] and
                            abs(metrics[2][3] - metrics[0][3]) < .01 and
                            metrics[1][1] > metrics[0][1] and
                            abs(metrics[2][1] - metrics[0][1]) < .1 and
                            all(0 < metrics[i][0] < 16 for i in range(5)) and
                            abs(metrics[4][2] - (metrics[4][1] - 170)) < .1 and
                            metrics[5][0] == 3 and metrics[5][1] < 170 and
                            metrics[5][2] == 0):
                        errors.append(f"ui-variable: grow/shrink/anchor/range failed: {metrics}")
                else:
                    if not (all(metrics[i][1] > 500 and metrics[i][2] > 500 and
                                abs(metrics[i][3] - (metrics[i][2] - 330)) < .1
                                for i in (1, 4)) and
                            all(metrics[i][1] < 100 and metrics[i][2] < 330 and
                                metrics[i][3] == 0 for i in (0, 2, 3, 5))):
                        errors.append(f"ui-stress: short/long/short/scroll failed: {metrics}")
        # Compile and execute every other migrated once-only UI caller, not just
        # parse its catalog entry. These are smoke runs, not long gameplay tests.
        for flag in MIGRATED_CALLERS:
            rc, log = run_hello(hello, ["--headless", "--frames", "3", f"--{flag}-demo"])
            if (rc or script_error_hits(log)
                    or any(x in log for x in ("update() failed", "[UI_OVERFLOW]", "[ERROR]"))):
                errors.append(f"{flag}: migrated caller smoke failed (exit {rc})\n{log}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    add_hello_argument(parser)
    args = parser.parse_args()
    hello = resolve_hello(args.hello)
    errors = validate_ui_demos(hello)
    if not errors:
        print("check_ui_demos: PASS (layout/features/variable/stress + migrated callers)")
    return die_with_errors("check_ui_demos", errors)


if __name__ == "__main__":
    raise SystemExit(main())
