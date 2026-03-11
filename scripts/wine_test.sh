#!/usr/bin/env bash
# wine_test.sh — run the MinGW/Wine ctest suite with a persistent wineserver
# so subsequent wine process launches reuse the already-running server and
# skip its ~4s initialisation cost.
#
# Usage:
#   ./scripts/wine_test.sh [extra ctest args...]
#
# Safe to Ctrl-C: the EXIT trap kills the persistent wineserver.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------------------------------------------------------------------
cleanup() {
    echo "[wine_test] Stopping wineserver..."
    wineserver -k 2>/dev/null || true
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Ensure a Wine prefix exists before starting the server.
if [[ ! -d "${WINEPREFIX:-${HOME}/.wine}/drive_c" ]]; then
    echo "[wine_test] Initialising Wine prefix (one-time)..."
    WINEDEBUG=-all wineboot --init 2>/dev/null || true
    sleep 2
fi

# ---------------------------------------------------------------------------
# Start a persistent wineserver.
#   -f  = run in foreground (we background it ourselves)
#   -p0 = stay alive until explicitly killed (don't exit when last client quits)
echo "[wine_test] Starting persistent wineserver..."
WINEDEBUG=-all wineserver -f -p0 &
sleep 0.5   # allow server socket to be ready before tests fire

# ---------------------------------------------------------------------------
# Run ctest via the mingw-debug preset (configured for -j8).
echo "[wine_test] Running ctest..."
WINEDEBUG=-all ctest --preset mingw-debug "$@"
STATUS=$?

exit $STATUS
