#!/usr/bin/env bash
# verify.sh - Phase 2 end-to-end verification.
#
# Runs:
#   1. The host test suite (g++ + advdeck testing harness).
#   2. The PlatformIO firmware build for the m5stack-cardputer env.
#   3. The Python bridge test suite (pytest, click CLI, schemas).
#
# All three must exit 0. The script exits non-zero on the first
# failure and prints a clear "VERIFY OK" line at the end so a CI job
# can grep for it.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REPO_ROOT="$(cd "$ROOT/../.." && pwd)"
cd "$ROOT"

echo "=== Host tests (g++ + advdeck test harness) ==="
make -C test/host clean >/dev/null
make -C test/host test

echo
echo "=== Firmware build (platformio) ==="
/home/pi/.platformio/penv/bin/platformio run

echo
if [ -d "$REPO_ROOT/bridge/advdeck-agent-bridge" ]; then
  (cd "$REPO_ROOT/bridge/advdeck-agent-bridge" && \
    if [ ! -d .venv ]; then python3 -m venv .venv; fi && \
    .venv/bin/pip install -e .[test] --quiet && \
    .venv/bin/python -m pytest tests/ -q)
fi

echo
echo "VERIFY OK"
