cmake_minimum_required(VERSION 3.15)

# Library target
add_library(aiSocksLib STATIC
    src/Socket.cpp
    src/SocketImpl.cpp
    src/SocketImpl.h
)

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

# Platform-specific libraries
if(WIN32)
    target_link_libraries(aiSocksLib PRIVATE ws2_32)
endif()

# Compiler-specific settings
if(MSVC)
    target_compile_options(aiSocksLib PRIVATE /W4)
else()
    target_compile_options(aiSocksLib PRIVATE -Wall -Wextra -Wpedantic)
endif()
