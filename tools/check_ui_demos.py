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


from integration_bridge import ui_cases, run_cases


def validate_ui_demos(hello, errors=None):
    if errors is None:
        errors = []
    hello = resolve_hello(hello)
    with tempfile.TemporaryDirectory(prefix="fake2d-ui-demos-") as directory:
        if run_cases(hello, ui_cases(hello, directory)):
            errors.append("native Google Test dynamic UI cases failed (see assertions above)")
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
