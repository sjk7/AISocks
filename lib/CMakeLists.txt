cmake_minimum_required(VERSION 3.15)

# Library target
add_library(aiSocksLib STATIC
    src/pch.cpp
    src/Socket.cpp
    src/TcpSocket.cpp
    src/UdpSocket.cpp
    src/SocketImpl.cpp
    src/SocketImpl.h
    src/SocketImplHelpers.cpp
    src/SocketFactory.cpp
    src/Result.cpp
)

# Platform-specific Poller backend (exactly one is compiled)
if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
    target_sources(aiSocksLib PRIVATE src/PollerKqueue.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(aiSocksLib PRIVATE src/PollerEpoll.cpp)
elseif(WIN32)
    target_sources(aiSocksLib PRIVATE src/PollerWSAPoll.cpp)
endif()

# Set C++17 standard
target_compile_features(aiSocksLib PUBLIC cxx_std_17)

# Include directories
target_include_directories(aiSocksLib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Platform-specific libraries and compile definitions
if(WIN32)
    target_link_libraries(aiSocksLib PRIVATE ws2_32 iphlpapi mswsock winmm)
    # Prevent Windows.h from defining min/max macros and reduce header bloat
    target_compile_definitions(aiSocksLib PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    
    # Configure precompiled headers for MSVC
    target_precompile_headers(aiSocksLib PRIVATE src/pch.h)
endif()

# Compiler-specific settings
if(MSVC)
    target_compile_options(aiSocksLib PRIVATE /W4 /WX /utf-8 /MP)
else()
    target_compile_options(aiSocksLib PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-shadow)
    # Ensure debug symbols in Debug builds for Clang
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(aiSocksLib PRIVATE -g -O0)
    endif()
endif()


