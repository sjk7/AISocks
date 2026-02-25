#!/bin/bash

# WineSkin Wine wrapper script
# Usage: ./wine.sh <command> [args]

WINESKIN_WRAPPER="/Users/stevekerr/Library/Application Support/Wineskin/Wrapper/Wineskin-3.0.6_3.app"
WINESKIN_ENGINE="$WINESKIN_WRAPPER/Contents/Wineskin.app/Contents/Resources"

if [ ! -d "$WINESKIN_WRAPPER" ]; then
    echo "Error: WineSkin wrapper not found at $WINESKIN_WRAPPER"
    exit 1
fi

# Set up Wine environment
export WINEPREFIX="$WINESKIN_WRAPPER/Contents/drive_c"
export WINESERVER="$WINESKIN_ENGINE/wineserver"
export WINELOADER="$WINESKIN_ENGINE/wine"

# Start wineserver if not running
if ! pgrep -f "wineserver.*Wineskin-3.0.6_3" > /dev/null; then
    echo "Starting WineSkin wineserver..."
    "$WINESERVER" -w
fi

# Run the command
exec "$WINELOADER" "$@"
