#!/usr/bin/env bash
set -euo pipefail

# Benchmark large-file serving behavior for aiSocks HttpFileServer using the
# existing advanced_file_server example.
#
# Measures three scenarios:
#  1) large-uncached  - adds query string to bypass file cache
#  2) large-reused    - repeated same URL (shows warm-path behavior)
#  3) small-reused    - cache-friendly small file baseline

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-relwithdebinfo}"
SERVER_BIN="${SERVER_BIN:-$BUILD_DIR/advanced_file_server}"
BENCH_ROOT="${BENCH_ROOT:-$ROOT_DIR/www}"
PORT="${PORT:-18080}"
REPEATS="${REPEATS:-8}"
LARGE_MB="${LARGE_MB:-16}"
SMALL_KB="${SMALL_KB:-32}"
AUTH="admin:secret"

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "Missing required command: $1" >&2
        exit 1
    }
}

require_cmd curl
require_cmd awk
require_cmd dd
require_cmd mktemp
require_cmd lsof

if [[ ! -x "$SERVER_BIN" ]]; then
    echo "Server binary not found at $SERVER_BIN. Building advanced_file_server..."
    cmake --build "$BUILD_DIR" --target advanced_file_server
fi

kill_port_users() {
    local port="$1"
    local pids
    pids="$(lsof -ti "tcp:$port" || true)"
    if [[ -z "$pids" ]]; then
        return
    fi

    echo "Port $port is already in use. Terminating: $pids"
    kill $pids 2>/dev/null || true

    for _ in {1..20}; do
        if [[ -z "$(lsof -ti "tcp:$port" || true)" ]]; then
            return
        fi
        sleep 0.05
    done

    pids="$(lsof -ti "tcp:$port" || true)"
    if [[ -n "$pids" ]]; then
        echo "Force-killing remaining processes on port $port: $pids"
        kill -9 $pids 2>/dev/null || true
    fi
}

TMP_ROOT="$(mktemp -d)"
SERVER_LOG="$TMP_ROOT/server.log"
LARGE_FILE_NAME="bench_large_${PORT}_$$.bin"
SMALL_FILE_NAME="bench_small_${PORT}_$$.bin"
LARGE_FILE_PATH="$BENCH_ROOT/$LARGE_FILE_NAME"
SMALL_FILE_PATH="$BENCH_ROOT/$SMALL_FILE_NAME"

cleanup() {
    if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$LARGE_FILE_PATH" "$SMALL_FILE_PATH"
    rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

mkdir -p "$BENCH_ROOT"

echo "Preparing benchmark files in $BENCH_ROOT"
dd if=/dev/zero of="$LARGE_FILE_PATH" bs=1m count="$LARGE_MB" status=none

dd if=/dev/zero of="$SMALL_FILE_PATH" bs=1k count="$SMALL_KB" status=none

echo "Starting server on port $PORT"
kill_port_users "$PORT"
"$SERVER_BIN" "$PORT" "$BENCH_ROOT" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

for _ in {1..80}; do
    if curl -fsS -u "$AUTH" -o /dev/null "http://127.0.0.1:$PORT/" 2>/dev/null; then
        break
    fi
    sleep 0.05
done

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "Server exited unexpectedly. Log:" >&2
    sed -n '1,160p' "$SERVER_LOG" >&2
    exit 1
fi

run_case() {
    local name="$1"
    local url_base="$2"
    local mode="$3"
    local i url line t

    local samples=""
    local bytes=""

    for ((i=1; i<=REPEATS; i++)); do
        if [[ "$mode" == "uncached" ]]; then
            url="$url_base?run=$i"
        else
            url="$url_base"
        fi

        line="$(curl -fsS -u "$AUTH" -o /dev/null -w '%{time_total} %{size_download}' "$url")"
        t="$(awk '{print $1}' <<<"$line")"
        bytes="$(awk '{print $2}' <<<"$line")"
        samples+="$t\n"
    done

    awk -v name="$name" -v bytes="$bytes" '
        BEGIN { min = 1e9; max = 0; sum = 0; n = 0; }
        {
            if ($1 == "") next;
            v = $1 + 0;
            if (v < min) min = v;
            if (v > max) max = v;
            sum += v;
            n++;
        }
        END {
            avg = (n > 0) ? (sum / n) : 0;
            mb = bytes / (1024.0 * 1024.0);
            printf("%-14s n=%-2d size=%7.2f MiB  avg=%7.4fs  min=%7.4fs  max=%7.4fs\n",
                name, n, mb, avg, min, max);
        }
    ' <<<"$(printf "%b" "$samples")"
}

echo
echo "Benchmark results (REPEATS=$REPEATS, LARGE_MB=$LARGE_MB, SMALL_KB=$SMALL_KB)"
echo "--------------------------------------------------------------------------"
run_case "large-uncached" "http://127.0.0.1:$PORT/$LARGE_FILE_NAME" "uncached"
run_case "large-reused"   "http://127.0.0.1:$PORT/$LARGE_FILE_NAME" "reused"
run_case "small-reused"   "http://127.0.0.1:$PORT/$SMALL_FILE_NAME" "reused"

echo
echo "Interpretation:"
echo "- large-uncached approximates no-cache path cost"
echo "- large-reused shows repeated request behavior for same large asset"
echo "- small-reused is the cache-friendly baseline"
