#!/usr/bin/env python3
"""Fail when headline counts in README.md / README_CN.md drift from the code.

The guard compares three sources of truth:
  * include/fake2d/entity_store.h  -> persistent entity slots
  * include/fake2d/demos.h         -> runnable demos
  * examples/hello/CMakeLists.txt  -> registered self-test suites
and, by running the built binary (unless --no-run), the assertion total that
the suites actually report. CI runs it after `ctest`, so an added suite or demo
that is not reflected in the docs fails the build instead of silently rotting.

  tools/check-docs.py [--no-run]
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

ROOT = Path(__file__).resolve().parent.parent
README = ROOT / "README.md"
README_CN = ROOT / "README_CN.md"
DEMOS_H = ROOT / "include/fake2d/demos.h"
ENTITY_STORE_H = ROOT / "include/fake2d/entity_store.h"
HELLO_CMAKE = ROOT / "examples/hello/CMakeLists.txt"
BINARY = ROOT / "build/bin/fake2d_hello"

errors: list[str] = []


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
            errors.append(f"suite {suite} failed while counting assertions")
            continue
        match = re.search(r"PASS: (\d+) checks", proc.stdout + proc.stderr)
        if not match:
            errors.append(f"suite {suite} printed no 'PASS: N checks' line")
            continue
        total += int(match.group(1))
    return total


def validate_ui_demos() -> None:
    """Compile/run the real FakeLua scenarios; host logs update errors but
    historically exits 0, so checking only the process status is insufficient."""
    prefix = []
    if not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
        prefix = ["xvfb-run", "-a", "-s", "-screen 0 1024x768x24"]
    with tempfile.TemporaryDirectory(prefix="fake2d-ui-demos-") as directory:
        for flag, markers, shot in [("ui-layout", range(148000, 148006), 60),
                                    ("ui-features", range(148100, 148103), 100)]:
            image = Path(directory) / (flag + ".png")
            proc = subprocess.run([*prefix, str(BINARY), "--headless", "--strict-assets",
                                   "--frames", "240", f"--{flag}-demo", "--screenshot",
                                   str(image), "--shot-frame", str(shot), "--shot-min-colors", "12"],
                                  cwd=ROOT, capture_output=True, text=True)
            log = proc.stdout + proc.stderr
            if proc.returncode or not image.is_file() or any(x in log for x in
                    ("update() failed", "[UI_OVERFLOW]", "strict assets:", "[ERROR]")):
                errors.append(f"{flag}: dynamic UI demo failed\n{log}")
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-run", action="store_true",
                        help="skip executing suites (docs-only check)")
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
        total = assertion_total(suites)
        validate_ui_demos()

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
