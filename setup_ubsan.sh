#!/bin/bash
set -e

echo "Setting up UndefinedBehaviorSanitizer build configuration for http_server..."

# Create build directory for UBSan
mkdir -p build-ubsan

# Configure CMake with UBSan and examples (RelWithDebInfo = -O2 -g -DNDEBUG)
cmake -S . -B build-ubsan \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DENABLE_UBSAN=ON \
    -DBUILD_EXAMPLES=ON \
    -G Ninja

echo "UndefinedBehaviorSanitizer build configured!"
echo "Run: cmake --build build-ubsan --target http_server"
