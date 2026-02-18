# Cross-compilation toolchain: macOS (arm64) → Windows x86-64 via MinGW-w64
# Usage:
#   cmake -S . -B build-mingw \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
#         -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON \
#         -G Ninja
#   ninja -C build-mingw

set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_VERSION 10)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_PREFIX /opt/homebrew/bin/x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${MINGW_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${MINGW_PREFIX}-windres)
set(CMAKE_AR           ${MINGW_PREFIX}-ar)
set(CMAKE_RANLIB       ${MINGW_PREFIX}-ranlib)

# Sysroot for MinGW headers/libraries
set(CMAKE_FIND_ROOT_PATH /opt/homebrew/x86_64-w64-mingw32)

# Only look in the target sysroot for libraries/includes, not host paths.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# MinGW produces Windows PE binaries; linking ws2_32 etc. works normally.
