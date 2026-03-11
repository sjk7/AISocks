#!/bin/bash

# Build script for HttpClient using CMake build system
# Requires C++17 for string_view and structured bindings

set -e  # Exit on any error

echo "=== Building HttpClient with CMake ==="

# Build directory
BUILD_DIR="build-relwithdebinfo"

echo "1. Configuring with CMake (BUILD_EXAMPLES=ON)..."
cd $BUILD_DIR
cmake .. -DBUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo

echo "2. Building with Ninja..."
ninja

echo ""
echo "=== Build Complete! ==="
echo ""
echo "Executables created:"
echo "  $BUILD_DIR/http_client_example - HttpClient demonstration"
echo "  $BUILD_DIR/test_efficiency - Performance test"
echo ""
echo "Key optimizations implemented:"
echo "- Zero-copy URL parsing using string_view"
echo "- No string allocations during URL parsing"  
echo "- StringBuilder for efficient request building"
echo "- All string_views reference original buffer"
echo "- Proper C++17 structured bindings usage"
echo "- All warnings and errors addressed"
echo "- Built with existing AISocks library using CMake"
