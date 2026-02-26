#!/bin/bash
set -e

echo "Setting up MemorySanitizer build configuration for http_server..."

# Create build directory for MSan
mkdir -p build-msan

# Configure CMake with MemorySanitizer and examples
cmake -S . -B build-msan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_MSAN=ON \
    -DBUILD_EXAMPLES=ON \
    -G Ninja

echo "MemorySanitizer build configured!"
echo "Run: cmake --build build-msan --target http_server"
echo ""
echo "Note: MemorySanitizer requires all dependencies to be built with MSan."
echo "For system libraries, you may need to use a MSan-instrumented runtime."
