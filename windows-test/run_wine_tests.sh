#!/bin/bash

# Run AISocks Windows tests through WineSkin

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WINE_WRAPPER="$SCRIPT_DIR/wine.sh"
TEST_DIR="$SCRIPT_DIR"

echo "=== Running AISocks Tests in WineSkin ==="
echo "Wine Wrapper: $WINE_WRAPPER"
echo "Test Directory: $TEST_DIR"
echo.

# Test a simple executable first
echo "[1/5] Testing minimal server..."
"$WINE_WRAPPER" "$TEST_DIR/test_server_base_minimal.exe"
echo.

echo "[2/5] Testing echo server..."
"$WINE_WRAPPER" "$TEST_DIR/test_server_base_echo_simple.exe"
echo.

echo "[3/5] Testing error messages..."
"$WINE_WRAPPER" "$TEST_DIR/test_error_messages.exe"
echo.

echo "[4/5] Testing main example..."
"$WINE_WRAPPER" "$TEST_DIR/aiSocksExample.exe"
echo.

echo "[5/5] Testing socket factory..."
"$WINE_WRAPPER" "$TEST_DIR/test_socket_factory.exe"
echo.

echo "=== Wine Tests Completed ==="
