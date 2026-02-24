#!/bin/bash

# Run Windows executable with timeout and output capture

WINESKIN_LAUNCHER="/Users/stevekerr/Library/Application Support/Wineskin/Wrapper/Wineskin-3.0.6_3.app/Contents/MacOS/wineskinlauncher"
EXECUTABLE="$1"
TIMEOUT="${2:-10}"  # Default 10 second timeout

if [ -z "$EXECUTABLE" ]; then
    echo "Usage: $0 <executable.exe> [timeout_seconds]"
    exit 1
fi

echo "=== Running $EXECUTABLE (timeout: ${TIMEOUT}s) ==="
echo "Time: $(date)"
echo "Launcher: $WINESKIN_LAUNCHER"
echo "Executable: $EXECUTABLE"
echo.

# Run with timeout and capture any output
timeout "$TIMEOUT" "$WINESKIN_LAUNCHER" "$EXECUTABLE" 2>&1
EXIT_CODE=$?

echo.
echo "=== Test Complete ==="
echo "Exit code: $EXIT_CODE"
echo "Time: $(date)"

if [ $EXIT_CODE -eq 124 ]; then
    echo "⚠️  Test timed out after ${TIMEOUT} seconds"
elif [ $EXIT_CODE -eq 0 ]; then
    echo "✅ Test passed"
else
    echo "❌ Test failed with exit code $EXIT_CODE"
fi
