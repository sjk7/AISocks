cmake_minimum_required(VERSION 3.15)

# Prefer Ninja when available. The CMakePresets.json base preset requests Ninja;
# this check fires early so the user gets a clear message instead of a cryptic
# "could not find make program" error, with an obvious fallback command.
if(CMAKE_GENERATOR STREQUAL "Ninja")
    # Also check alongside the cmake binary itself — the cmake.org official
    # installer bundles ninja next to cmake, even if it's not in PATH.
    get_filename_component(_cmake_dir "${CMAKE_COMMAND}" DIRECTORY)
    find_program(NINJA_EXE ninja HINTS "${_cmake_dir}")
    if(NOT NINJA_EXE)
        message(FATAL_ERROR
            "Ninja generator requested but 'ninja' was not found in PATH.\n"
            "  Install Ninja:  brew install ninja   # macOS\n"
            "                  sudo apt install ninja-build  # Linux\n"
            "  Or fall back:   cmake -G 'Unix Makefiles' <source-dir>")
    endif()
    message(STATUS "Ninja found: ${NINJA_EXE}")
endif()

project(aiSocks VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Always generate compile_commands.json so clangd can find headers.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable warnings as errors
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wconversion -Wno-sign-conversion -Werror)
endif()

# Build options
# Tests are built by default, examples are opt-in
option(BUILD_TESTS    "Build test programs"    ON)
option(BUILD_EXAMPLES "Build all example programs" OFF)
option(ENABLE_ASAN    "Enable AddressSanitizer"    OFF)
option(ENABLE_MSAN    "Enable MemorySanitizer"     OFF)
option(ENABLE_UBSAN   "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN    "Enable ThreadSanitizer"     OFF)
option(ENABLE_COVERAGE "Enable Clang source-based code coverage" OFF)
option(AISOCKS_ENABLE_TLS "Enable TLS/HTTPS support via OpenSSL" ON)

if(AISOCKS_ENABLE_TLS)
    # Prefer shared OpenSSL to use system dynamic libraries.
    set(OPENSSL_USE_STATIC_LIBS FALSE)
    find_package(OpenSSL REQUIRED)
    message(STATUS "TLS enabled: OpenSSL ${OPENSSL_VERSION}")
endif()

# Add AddressSanitizer if enabled
if(ENABLE_ASAN AND MSVC)
    add_compile_definitions(_ENABLE_ASAN)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.13")
        add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
        add_link_options($<$<CONFIG:Debug>:/fsanitize=address>)
    endif()
elseif(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Add UndefinedBehaviorSanitizer if enabled
if(ENABLE_UBSAN AND NOT MSVC)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
    message(STATUS "UndefinedBehaviorSanitizer enabled")
elseif(ENABLE_UBSAN AND MSVC)
    message(FATAL_ERROR "UndefinedBehaviorSanitizer is not supported on MSVC/Windows")
endif()

# Add ThreadSanitizer if enabled
if(ENABLE_TSAN AND NOT MSVC)
    if(ENABLE_ASAN OR ENABLE_MSAN)
        message(FATAL_ERROR "ThreadSanitizer cannot be combined with ASan or MSan")
    endif()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
    message(STATUS "ThreadSanitizer enabled")
elseif(ENABLE_TSAN AND MSVC)
    message(FATAL_ERROR "ThreadSanitizer is not supported on MSVC/Windows")
endif()

# Add MemorySanitizer if enabled
if(ENABLE_MSAN AND NOT MSVC)
    if(APPLE)
        message(FATAL_ERROR "MemorySanitizer is not supported on macOS (including Apple Silicon). Use Linux for MSan.")
    endif()
    # Suppress false positives from uninstrumented libc++ (installed from apt,
    # not built with MSan). Without this, std::string etc. report
    # use-of-uninitialized-value because libc++ internals aren't tracked.
    set(MSAN_IGNORELIST "${CMAKE_SOURCE_DIR}/msan_ignorelist.txt")
    add_compile_options(
        -fsanitize=memory
        -fno-omit-frame-pointer
        -fsanitize-ignorelist=${MSAN_IGNORELIST}
    )
    add_link_options(-fsanitize=memory)
    message(STATUS "MemorySanitizer enabled - use RelWithDebInfo for -O2 -g (recommended)")
elseif(ENABLE_MSAN AND MSVC)
    message(FATAL_ERROR "MemorySanitizer is not supported on MSVC/Windows")
endif()

# Add source-based coverage instrumentation if enabled
if(ENABLE_COVERAGE AND MSVC)
    message(FATAL_ERROR "Source-based coverage (llvm-cov) is not supported on MSVC.")
elseif(ENABLE_COVERAGE)
    if(ENABLE_ASAN OR ENABLE_MSAN OR ENABLE_UBSAN OR ENABLE_TSAN)
        message(FATAL_ERROR "ENABLE_COVERAGE cannot be combined with sanitizers")
    endif()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping -fno-omit-frame-pointer)
    add_link_options(-fprofile-instr-generate)
    message(STATUS "Source-based coverage enabled (use scripts/coverage.sh to generate report)")
endif()

# Add the library
add_subdirectory(lib)

# Primary example — always built so the library is exercised out of the box.
add_executable(aiSocksExample examples/main.cpp)
target_link_libraries(aiSocksExample PRIVATE aiSocksLib)
if(MSVC)
    target_compile_options(aiSocksExample PRIVATE /utf-8 /MP)
else()
    # Ensure debug symbols in Debug builds for Clang
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(aiSocksExample PRIVATE -g -O0)
    endif()
endif()
# Set the startup project for Visual Studio
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT aiSocksExample)

# Enable CTest
if(BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

# Extra examples (opt-in via -DBUILD_EXAMPLES=ON)
# Examples intentionally test the PUBLIC header surface only (no
# SocketImpl.h).  Do NOT add PCH REUSE_FROM or UNITY_BUILD here — they
# would pull platform headers into the example TUs and break the
# SOMAXCONN firewall canary in examples/test_ip_utils.cpp.
if(BUILD_EXAMPLES)
    set(_EXAMPLES
        aiSocksNonBlocking      examples/nonblocking.cpp
        aiSocksTestBlocking     examples/test_blocking_state.cpp
        aiSocksTestIPv6         examples/test_ipv6.cpp
        aiSocksTestIPUtils      examples/test_ip_utils.cpp
        aiSocksTestMove         examples/test_move_semantics.cpp
        aiSocksPeerLogger       examples/peer_logger.cpp
        aiSocksGoogleClient     examples/google_client.cpp
        aiSocksSocketFactoryDemo examples/socket_factory_demo.cpp
        low_level_http_server   examples/low_level_http_server.cpp
        simple_file_server      examples/simple_file_server.cpp
        advanced_file_server    examples/advanced_file_server.cpp
        throughput_client       examples/throughput_client.cpp
        unix_throughput         examples/unix_throughput.cpp
        http_client_example      examples/http_client_example.cpp
        test_efficiency          test_efficiency.cpp
    )
    list(LENGTH _EXAMPLES _len)
    math(EXPR _last "${_len} - 2")
    foreach(_i RANGE 0 ${_last} 2)
        math(EXPR _j "${_i} + 1")
        list(GET _EXAMPLES ${_i} _tgt)
        list(GET _EXAMPLES ${_j} _src)
        add_executable(${_tgt} ${_src})
        if(_tgt STREQUAL "advanced_file_server")
            target_sources(${_tgt} PRIVATE examples/advanced_file_server_pages.cpp)
        endif()
        target_link_libraries(${_tgt} PRIVATE aiSocksLib)
        
        # Uncomment to enable connection stats logging (impacts performance)
        # if(_tgt STREQUAL "http_server")
        #     target_compile_definitions(${_tgt} PRIVATE SERVER_STATS)
        # endif()
        if(MSVC)
            target_compile_options(${_tgt} PRIVATE /utf-8 /MP)
        else()
            # Ensure debug symbols in Debug builds for Clang
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                target_compile_options(${_tgt} PRIVATE -g -O0)
            endif()
            # On macOS with Homebrew LLVM: link against its libc++ so template
            # instantiations resolve symbols like __hash_memory.
            if(APPLE)
                get_filename_component(_EX_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
                get_filename_component(_EX_LLVM_ROOT "${_EX_COMPILER_DIR}" DIRECTORY)
                if(EXISTS "${_EX_LLVM_ROOT}/lib/c++")
                    target_link_directories(${_tgt} PRIVATE "${_EX_LLVM_ROOT}/lib/c++")
                    set_target_properties(${_tgt} PROPERTIES
                        BUILD_RPATH "${_EX_LLVM_ROOT}/lib/c++")
                endif()
            endif()
        endif()
    endforeach()
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "aiSocks Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Examples: ${BUILD_EXAMPLES}")
message(STATUS "  TLS Enabled: ${AISOCKS_ENABLE_TLS}")
if(AISOCKS_ENABLE_TLS)
    message(STATUS "  OpenSSL Version: ${OPENSSL_VERSION}")
endif()
message(STATUS "  System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Generator: ${CMAKE_GENERATOR}")
message(STATUS "")

# Copy compile_commands.json to source root for IDE/clangd support (cross-platform)
# This runs at configure time, then users can manually update it by reconfiguring
# or by running: cmake --build build-ninja --target copy_compile_commands
# NOTE: compile_commands.json is only generated by Makefile and Ninja generators.
if(CMAKE_EXPORT_COMPILE_COMMANDS AND EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json")
    file(COPY "${CMAKE_BINARY_DIR}/compile_commands.json" 
         DESTINATION "${CMAKE_SOURCE_DIR}")
    message(STATUS "Copied compile_commands.json to source root")
endif()

# Optional manual target to update compile_commands.json after builds
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(copy_compile_commands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COMMENT "Copying compile_commands.json to source root"
        VERBATIM
    )
endif()


