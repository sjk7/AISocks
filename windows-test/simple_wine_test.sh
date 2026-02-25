#!/bin/bash

# Simple Wine test - try different approaches

WINESKIN="/Users/stevekerr/Library/Application Support/Wineskin/Wrapper/Wineskin-3.0.6_3.app"
WINE_RESOURCES="$WINESKIN/Contents/Wineskin.app/Contents/Resources"
TEST_EXE="./test_server_base_minimal.exe"

echo "=== Testing WineSkin Wine Access ==="
echo "WineSkin: $WINESKIN"
echo "Wine Resources: $WINE_RESOURCES"
echo.

# Method 1: Direct wine binary
echo "Method 1: Direct wine binary..."
if [ -f "$WINE_RESOURCES/wine" ]; then
    echo "Found wine binary at: $WINE_RESOURCES/wine"
    cd "$(dirname "$0")"
    WINEPREFIX="$WINESKIN/Contents/drive_c" "$WINE_RESOURCES/wine" "$TEST_EXE"
    echo "Exit code: $?"
else
    echo "Wine binary not found"
fi
echo.

# Method 2: Through Wineskin app
echo "Method 2: Through Wineskin app..."
if [ -f "$WINESKIN/Contents/MacOS/Wineskin" ]; then
    echo "Found Wineskin launcher"
    cd "$(dirname "$0")"
    "$WINESKIN/Contents/MacOS/Wineskin" "$TEST_EXE"
    echo "Exit code: $?"
else
    echo "Wineskin launcher not found"
fi

echo "=== Wine Test Complete ==="
