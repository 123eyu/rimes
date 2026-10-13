#!/usr/bin/env bash
# Unit tests, librime smoke, and headless Fcitx5 E2E.
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
IME_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd -P)
BUILD_DIR="${RIMES_IME_BUILD_DIR:-$IME_ROOT/build}"

python3 -m unittest discover -s "$IME_ROOT/tests" -p 'test_*.py'
"$SCRIPT_DIR/build.sh"
ctest --test-dir "$BUILD_DIR" --output-on-failure
"$IME_ROOT/tests/e2e/e2e.sh" --build-dir "$BUILD_DIR"
