#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
JOBS="${JOBS:-8}"

if [[ "${1:-}" == "-j" ]]; then
    JOBS="${2:-8}"
    shift 2 || true
fi

if [[ "${1:-}" == "--jobs" ]]; then
    JOBS="${2:-8}"
    shift 2 || true
fi

if ! [[ "$JOBS" =~ ^[0-9]+$ ]] || [[ "$JOBS" -lt 1 ]]; then
    echo "error: jobs must be a positive integer" >&2
    exit 2
fi

WORK_DIR="$(mktemp -d /tmp/aisocks-preset-compare.XXXXXX)"

measure_cmd_real() {
    local log_file="$1"
    shift

    (
        cd "$ROOT_DIR"
        /usr/bin/time -p "$@"
    ) >"${log_file}.out" 2>"${log_file}.err"

    awk '/^real / { print $2; found=1 } END { if (!found) exit 1 }' "${log_file}.err"
}

run_preset() {
    local preset="$1"
    local build_dir="$2"

    echo "" >&2
    echo "=== Running preset: ${preset} (jobs=${JOBS}) ===" >&2

    rm -rf "$ROOT_DIR/$build_dir"

    local cfg_real build_real test_real
    cfg_real="$(measure_cmd_real "$WORK_DIR/${preset}-configure" cmake --preset "$preset")"
    build_real="$(measure_cmd_real "$WORK_DIR/${preset}-build" cmake --build --preset "$preset" -j"$JOBS")"
    test_real="$(measure_cmd_real "$WORK_DIR/${preset}-test" ctest --test-dir "$ROOT_DIR/$build_dir" -j"$JOBS" --output-on-failure)"

    local test_count
    test_count="$(grep -Eo '[0-9]+/[0-9]+ tests passed' "$WORK_DIR/${preset}-test.out" | tail -n 1 || true)"
    if [[ -z "$test_count" ]]; then
        test_count="(test count not parsed)"
    fi

    local total
    total="$(awk -v a="$cfg_real" -v b="$build_real" -v c="$test_real" 'BEGIN{printf "%.2f", a+b+c}')"

    echo "configure: ${cfg_real}s" >&2
    echo "build:     ${build_real}s" >&2
    echo "tests:     ${test_real}s" >&2
    echo "total:     ${total}s" >&2
    echo "tests:     ${test_count}" >&2

    printf '%s\t%s\t%s\t%s\t%s\n' "$preset" "$cfg_real" "$build_real" "$test_real" "$total"
}

echo "Comparing presets from: $ROOT_DIR"
echo "Using parallel jobs: $JOBS"

declare -a rows
rows+=("$(run_preset "relwithdebinfo" "build-relwithdebinfo")")
rows+=("$(run_preset "fast-dev" "build-fastdev")")

rel_row="${rows[0]}"
fast_row="${rows[1]}"

IFS=$'\t' read -r _ rel_cfg rel_build rel_test rel_total <<< "$rel_row"
IFS=$'\t' read -r _ fast_cfg fast_build fast_test fast_total <<< "$fast_row"

saved_total="$(awk -v a="$rel_total" -v b="$fast_total" 'BEGIN{printf "%.2f", a-b}')"
saved_pct="$(awk -v a="$rel_total" -v b="$fast_total" 'BEGIN{if (a>0) printf "%.1f", ((a-b)/a)*100; else print "0.0"}')"

saved_build="$(awk -v a="$rel_build" -v b="$fast_build" 'BEGIN{printf "%.2f", a-b}')"
saved_test="$(awk -v a="$rel_test" -v b="$fast_test" 'BEGIN{printf "%.2f", a-b}')"

echo ""
echo "=== Summary (relwithdebinfo -> fast-dev) ==="
echo "total: ${rel_total}s -> ${fast_total}s (saved ${saved_total}s, ${saved_pct}%)"
echo "build: ${rel_build}s -> ${fast_build}s (saved ${saved_build}s)"
echo "tests: ${rel_test}s -> ${fast_test}s (saved ${saved_test}s)"
echo ""
echo "Logs saved in: $WORK_DIR"
