#!/usr/bin/env bash
# wine_test.sh — run the MinGW/Wine ctest suite.
# Starts a persistent wineserver so each wine process reuses it (~0.6s startup)
# instead of spawning its own (~5s cold start).
#
# Usage:
#   ./scripts/wine_test.sh [extra ctest args...]

set -euo pipefail

WINESERVER_REAL=$(realpath "$(which wineserver)" 2>/dev/null || which wineserver)

# Kill any leftover wineserver, then start a fresh persistent one.
# -p with no argument = stay alive indefinitely after the last client exits.
# No -f: without foreground mode wineserver daemonizes cleanly and does not
# hold the tty open.
wineserver -k 2>/dev/null || true
"$WINESERVER_REAL" -p &
sleep 0.5   # give the daemon time to create its socket

echo "Wineserver PID before prime: $(pgrep -f wineserver || echo NONE)"

# Run one wine process to completion to fully initialize the Wine prefix.
# Without this, all 8 parallel ctest workers race to initialize through the
# same wineserver simultaneously, causing a deadlock hang.
echo "Priming Wine prefix..."
WINEDEBUG=-all wine build-mingw/tests/test_result.exe > /dev/null 2>&1 || true

echo "Wineserver PID after prime:  $(pgrep -f wineserver || echo NONE)"

WINEDEBUG=-all ctest --preset mingw-debug "$@"
STATUS=$?

wineserver -k 2>/dev/null || true
exit $STATUS
