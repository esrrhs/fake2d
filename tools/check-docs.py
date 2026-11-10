#!/usr/bin/env python3
"""Fail when headline counts in README.md / README_CN.md drift from the code.

The guard compares three sources of truth:
  * include/fake2d/entity_store.h  -> persistent entity slots
  * include/fake2d/demos.h         -> runnable demos
  * examples/hello/CMakeLists.txt  -> registered self-test suites
and the assertion total that the suites actually report. CI runs it after
`ctest` and passes `--ctest-log` so the 30 suites are not executed a second
time; counts are parsed from the log (every suite must still have a PASS line).
`--no-run` skips execution entirely (docs-only). Dynamic UI demo acceptance
still runs via tools/check_ui_demos.py unless `--no-run`.

  tools/check-docs.py [--no-run] [--ctest-log PATH]
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from check_ui_demos import validate_ui_demos

ROOT = Path(__file__).resolve().parent.parent
README = ROOT / "README.md"
README_CN = ROOT / "README_CN.md"
DEMOS_H = ROOT / "include/fake2d/demos.h"
ENTITY_STORE_H = ROOT / "include/fake2d/entity_store.h"
HELLO_CMAKE = ROOT / "examples/hello/CMakeLists.txt"
BINARY = ROOT / "build/bin/fake2d_hello"

errors: list[str] = []

# 1.50 additions must remain discoverable to both C++ and FakeLua callers.
for symbol in ("SetVariableRowText", "SetVariableMeasureBudget", "ui_vlist_text",
               "ui_vlist_budget", "UI_UNSCOPED", "rich_batches"):
    if symbol not in (ROOT / "docs/SCRIPTING.md").read_text():
        errors.append(f"SCRIPTING.md: missing 1.50 migration/API term {symbol}")



def check(label: str, source: str, pattern: str, expected: int) -> None:
    text = (ROOT / source).read_text()
    found = re.findall(pattern, text)
    if not found:
        errors.append(f"{source}: pattern not found: {pattern}")
        return
    for value in found:
        if int(value) != expected:
            errors.append(
                f"{source}: {label} says {value}, code has {expected}")



# CTest / printf tag is the suite name except for this hyphenated alias.
SUITE_LOG_TAG = {
    "map-object": "map-obj",
}


def suite_pass_count(text: str, suite: str) -> int | None:
    tag = SUITE_LOG_TAG.get(suite, suite)
    matches = re.findall(
        rf"\[{re.escape(tag)}\] PASS: (\d+) checks", text)
    if not matches:
        fail = re.search(rf"\[{re.escape(tag)}\] FAIL(?:ED)?: (\d+) checks", text)
        if fail:
            errors.append(f"suite {suite} failed in the log ({fail.group(0)})")
        else:
            errors.append(f"suite {suite} printed no 'PASS: N checks' line")
        return None
    if len(set(matches)) != 1:
        errors.append(f"suite {suite} reported conflicting PASS counts: {matches}")
        return None
    return int(matches[0])


def assertion_total_from_log(path: Path, suites: list[str]) -> int | None:
    if not path.is_file():
        errors.append(f"missing ctest log: {path}")
        return None
    text = path.read_text(errors="replace")
    total = 0
    missing = False
    for suite in suites:
        count = suite_pass_count(text, suite)
        if count is None:
            missing = True
            continue
        total += count
    return None if missing else total


def assertion_total(suites: list[str]) -> int | None:
    if not BINARY.is_file():
        errors.append(f"missing test binary: {BINARY} (configure/build first)")
        return None
    prefix: list[str] = []
    if not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
        prefix = ["xvfb-run", "-a", "-s", "-screen 0 1024x768x24"]
    total = 0
    for suite in suites:
        proc = subprocess.run(
            [*prefix, str(BINARY), "--headless", f"--{suite}-test"],
            cwd=ROOT, capture_output=True, text=True)
        if proc.returncode != 0:
            errors.append(f"suite {suite} failed while counting assertions (exit {proc.returncode})\n"
                          f"{proc.stdout}{proc.stderr}")
            continue
        count = suite_pass_count(proc.stdout + proc.stderr, suite)
        if count is None:
            continue
        total += count
    return total


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-run", action="store_true",
                        help="skip executing suites (docs-only check)")
    parser.add_argument("--ctest-log", type=Path, default=None,
                        help="reuse PASS counts from a ctest --output-log instead of re-running suites")
    args = parser.parse_args()

    demos = len(re.findall(r'\{"[a-z0-9-]+"', DEMOS_H.read_text()))
    slots = int(re.search(r"kMaxEntities = (\d+)", ENTITY_STORE_H.read_text()).group(1))
    suite_block = re.search(r"set\(FAKE2D_TEST_SUITES\s+(.*?)\)", HELLO_CMAKE.read_text(),
                            re.S)
    if not suite_block:
        print("check-docs: ERROR: FAKE2D_TEST_SUITES not found", file=sys.stderr)
        return 1
    suites = suite_block.group(1).split()
    total = None
    if not args.no_run:
        if args.ctest_log is not None:
            total = assertion_total_from_log(args.ctest_log, suites)
        else:
            total = assertion_total(suites)
        validate_ui_demos(BINARY, errors)

    check("entity slots", "README.md",
          r"EntityStore` \((\d+) native handle-based entity slots\)", slots)
    check("entity slots", "README_CN.md",
          r"EntityStore`（(\d+) 个持久原生实体槽位）", slots)
    check("suite count", "README.md",
          r"\*\*(\d+) Built-in Self-Test Suites\*\*", len(suites))
    check("suite count", "README_CN.md",
          r"\*\*(\d+) 项内置自测试套件\*\*", len(suites))
    check("demo count", "README.md",
          r"comes with (\d+) rich, self-contained demos", demos)
    check("demo count", "README_CN.md",
          r"自带 (\d+) 个可直接运行", demos)
    check("launcher demo count", "README.md",
          r"Cover-art picker for all (\d+) demos", demos)
    check("launcher demo count", "README_CN.md",
          r"(\d+) 个 Demo 的封面图选择界面", demos)
    check("test instruction", "README.md",
          r"Run the (\d+) built-in engine self-test suites", len(suites))
    check("test instruction", "README_CN.md",
          r"运行 (\d+) 组内置无头自测试套件", len(suites))
    if total is not None:
        check("assertion total", "README.md",
              r"Built-in Self-Test Suites\*\*: (\d+) assertions", total)
        check("assertion total", "README_CN.md",
              r"项内置自测试套件\*\*：(\d+) 断言", total)

    if errors:
        for err in errors:
            print(f"check-docs: ERROR: {err}", file=sys.stderr)
        return 1
    suffix = f", {total} assertions" if total is not None else ""
    print(f"check-docs: demos={demos} suites={len(suites)} slots={slots}{suffix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
