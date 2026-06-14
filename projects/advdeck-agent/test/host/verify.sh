#!/usr/bin/env bash
# verify.sh - Phase 1 end-to-end verification.
#
# Runs:
#   1. The host test suite (g++ + advdeck testing harness).
#   2. The PlatformIO firmware build for the m5stack-cardputer env.
#
# Both must exit 0. The script exits non-zero on the first failure and
# prints a clear "VERIFY OK" / "VERIFY FAILED" line at the end so a CI
# job can grep for it.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

echo "=== Host tests (g++ + advdeck test harness) ==="
make -C test/host clean >/dev/null
make -C test/host test

echo
echo "=== Firmware build (platformio) ==="
/home/pi/.platformio/penv/bin/platformio run

echo
echo "VERIFY OK"
