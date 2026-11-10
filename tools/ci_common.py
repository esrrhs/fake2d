#!/usr/bin/env python3
"""Shared helpers for Fake2D CI scripts (Linux Xvfb, macOS CGL, Windows llvmpipe)."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
DEMOS_H = ROOT / "include/fake2d/demos.h"

# CompileFile still warns and continues without a script, so a zero exit is
# not enough. Engine::Run already returns non-zero after update() throws
# (see the --entry update_fail_test.lua inverse gate), but the log is still
# scanned so a future regression in that contract cannot hide.
SCRIPT_ERROR_RE = re.compile(r"syntax error|CompileFile.*failed|update\(\) failed")


def default_hello() -> Path:
    for name in ("fake2d_hello", "fake2d_hello.exe"):
        candidate = ROOT / "build" / "bin" / name
        if candidate.is_file():
            return candidate
    return ROOT / "build" / "bin" / "fake2d_hello"


def resolve_hello(path: Path | None) -> Path:
    hello = Path(path) if path is not None else default_hello()
    if not hello.is_file():
        raise SystemExit(f"missing test binary: {hello} (configure/build first)")
    return hello


def xvfb_prefix() -> list[str]:
    if not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
        return ["xvfb-run", "-a", "-s", "-screen 0 1024x768x24"]
    return []


def list_demos() -> list[str]:
    return re.findall(r'\{"([a-z0-9-]+)"', DEMOS_H.read_text())


def run_hello(hello: Path, args: list[str], cwd: Path | None = None
              ) -> tuple[int, str]:
    env = os.environ.copy()
    if xvfb_prefix():
        env.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
    cmd = [*xvfb_prefix(), str(hello), *args]
    proc = subprocess.run(cmd, cwd=cwd or ROOT, capture_output=True, text=True, env=env)
    log = (proc.stdout or "") + (proc.stderr or "")
    return proc.returncode, log


def script_error_hits(log: str) -> list[str]:
    return SCRIPT_ERROR_RE.findall(log)


def add_hello_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--hello", type=Path, default=None,
        help="Path to fake2d_hello (default: build/bin/fake2d_hello[.exe])")


def die_with_errors(prefix: str, errors: list[str]) -> int:
    if not errors:
        return 0
    for err in errors:
        print(f"{prefix}: ERROR: {err}", file=sys.stderr)
    return 1
