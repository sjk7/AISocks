#!/bin/bash
set -e

echo "Available build targets in aiSocks:"
echo "=================================="

if [ -d "build" ]; then
    echo ""
    echo "Debug build targets:"
    cmake --build build --target help 2>/dev/null | grep -E "^[a-zA-Z]" | head -20
fi

if [ -d "build-msan" ]; then
    echo ""
    echo "MemorySanitizer build targets:"
    cmake --build build-msan --target help 2>/dev/null | grep -E "^[a-zA-Z]" | head -20
fi

if [ -d "build-asan" ]; then
    echo ""
    echo "AddressSanitizer build targets:"
    cmake --build build-asan --target help 2>/dev/null | grep -E "^[a-zA-Z]" | head -20
fi

echo ""
echo "Available executables:"
find build* -name "aiSocks*" -type f -executable 2>/dev/null | sort
find build* -name "http_server" -type f -executable 2>/dev/null | sort

echo ""
echo "VS Code Debug Configurations:"
echo "Press F5 and select from:"
echo "  - Debug http_server (MemorySanitizer)"
echo "  - Debug http_server (AddressSanitizer)"
echo "  - Debug http_server (Debug)"
echo "  - Debug aiSocksExample (Debug)"
echo "  - Debug aiSocksNonBlocking (Debug)"
echo "  - Debug aiSocksGoogleClient (Debug)"
