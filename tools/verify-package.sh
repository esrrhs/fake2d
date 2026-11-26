#!/usr/bin/env bash
# Build/run adapter only. Every original package predicate is native Google Test.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
if command -v python3 >/dev/null 2>&1; then
    PYTHON=python3
else
    PYTHON=python
fi
exec "$PYTHON" "$REPO/tools/verify_package.py" "$@"
