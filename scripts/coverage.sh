#!/bin/bash
# Generate an HTML code-coverage report using Clang source-based coverage.
#
# Usage:
#   scripts/coverage.sh [--jobs N] [--report-dir PATH]
#
# Options:
#   --jobs N          Parallel build/test jobs (default: logical CPU count)
#   --report-dir PATH Output directory for HTML report (default: coverage-report/)
#   --slow-tests      Also build/run slow tests (e.g. test_timeout_heap)
#
# Requirements:
#   - Clang (Apple Clang via Xcode satisfies this)
#   - xcrun llvm-profdata and xcrun llvm-cov (bundled with Xcode)
#
# The report focuses on library source under lib/; test and example files
# are excluded so the numbers reflect production-code coverage only.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# --- Defaults ---------------------------------------------------------------
JOBS=$(sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null || echo 4)
REPORT_DIR="$ROOT/coverage-report"
SLOW_TESTS="OFF"

# --- Parse arguments --------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --jobs)       JOBS="$2";        shift 2 ;;
        --report-dir) REPORT_DIR="$2";  shift 2 ;;
        --slow-tests) SLOW_TESTS="ON";  shift   ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

BUILD_DIR="$ROOT/build-coverage"
PROFILE_DIR="$BUILD_DIR/coverage-profiles"
MERGED_PROFDATA="$BUILD_DIR/coverage.profdata"

echo "============================================================"
echo "  AISocks — Code Coverage Report"
echo "  Build dir : $BUILD_DIR"
echo "  Report dir: $REPORT_DIR"
echo "============================================================"
echo ""

# --- Step 1: Configure ------------------------------------------------------
echo "==> [1/5] Configuring coverage build..."
cmake --preset coverage \
    -DALLOW_SLOW_TESTS="$SLOW_TESTS" \
    -S "$ROOT" 2>&1
echo ""

# --- Step 2: Build all tests ------------------------------------------------
echo "==> [2/5] Building tests (jobs=$JOBS)..."
cmake --build "$BUILD_DIR" --config Debug -j"$JOBS"
echo ""

# --- Step 3: Run tests, collecting raw profiles -----------------------------
echo "==> [3/5] Running tests..."
rm -rf "$PROFILE_DIR"
mkdir -p "$PROFILE_DIR"

LLVM_PROFILE_FILE="$PROFILE_DIR/%p-%m.profraw" \
    ctest --test-dir "$BUILD_DIR" \
          --output-on-failure \
          -j"$JOBS" || true   # don't abort on individual test failures

PROFRAW_COUNT=$(find "$PROFILE_DIR" -name '*.profraw' 2>/dev/null | wc -l | tr -d ' ')
if [[ "$PROFRAW_COUNT" -eq 0 ]]; then
    echo "ERROR: No .profraw files were generated. Check that tests ran successfully."
    exit 1
fi
echo "  Collected $PROFRAW_COUNT raw profile(s)."
echo ""

# --- Step 4: Merge profiles -------------------------------------------------
echo "==> [4/5] Merging profiles..."
xcrun llvm-profdata merge \
    -sparse \
    "$PROFILE_DIR"/*.profraw \
    -o "$MERGED_PROFDATA"
echo "  Merged into: $MERGED_PROFDATA"
echo ""

# --- Step 5: Collect test executables ---------------------------------------
# Use while-read instead of mapfile so this works on macOS bash 3.2.
BINARIES=()
while IFS= read -r -d '' bin; do
    BINARIES+=("$bin")
done < <(
    find "$BUILD_DIR/tests" -maxdepth 1 -type f \( -perm -u+x -o -perm -g+x \) -print0 \
    | sort -z
)

if [[ ${#BINARIES[@]} -eq 0 ]]; then
    echo "ERROR: No test binaries found in $BUILD_DIR/tests/"
    exit 1
fi

# Use only the first binary for symbol/source mapping.
# The merged profdata already contains all runtime coverage from every test
# process.  Passing multiple -object flags would cause "mismatched data"
# warnings because the same library function gets compiled into each test
# binary with a different instrumentation hash.
FIRST="${BINARIES[0]}"

# --- Step 6: Generate HTML report and text summary --------------------------
echo "==> [5/5] Generating HTML report..."
rm -rf "$REPORT_DIR"
xcrun llvm-cov show \
    "$FIRST" \
    -instr-profile="$MERGED_PROFDATA" \
    -format=html \
    -output-dir="$REPORT_DIR" \
    -ignore-filename-regex='(tests/|examples/|/build[^/]*/|\.\./)' \
    -show-line-counts-or-regions \
    -show-branches=count \
    -Xdemangler=c++filt

echo ""
echo "============================================================"
echo "  Coverage summary  (library source only)"
echo "============================================================"
xcrun llvm-cov report \
    "$FIRST" \
    -instr-profile="$MERGED_PROFDATA" \
    -ignore-filename-regex='(tests/|examples/|/build[^/]*/|\.\./)' \
    -show-region-summary=false

echo ""
echo "HTML report: $REPORT_DIR/index.html"
echo "Open with:   open \"$REPORT_DIR/index.html\""
