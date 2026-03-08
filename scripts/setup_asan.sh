#!/bin/bash
set -e

echo "Setting up AddressSanitizer build configuration for http_server..."

# Create build directory for ASan
mkdir -p build-asan

# Configure CMake with AddressSanitizer and examples
cmake -S . -B build-asan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_ASAN=ON \
    -DBUILD_EXAMPLES=ON \
    -G Ninja

echo "AddressSanitizer build configured!"
echo "Run: cmake --build build-asan --target http_server"
