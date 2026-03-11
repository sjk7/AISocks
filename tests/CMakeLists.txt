set(TEST_SOURCES
    test_socket_basics
    test_ip_utils
    test_blocking
    test_move_semantics
    test_loopback_tcp
    test_error_handling
    test_construction
    test_new_features
    test_error_messages
    test_poller
    test_tcp_socket
    test_simple_client
    test_server_base
    test_server_base_edge_cases
    test_http_request
    test_url_codec
    test_socket_factory
    test_server_base_simple
    test_server_base_minimal
    test_server_base_echo_simple
    test_server_base_no_timeout
    test_fixes
    test_simple_server
    test_advanced_file_server
    test_file_io
    test_file_cache
    test_path_helper
    test_result
    test_server_signal
    test_http_poll_server
    test_extracted_components
    test_file_server_utils
    test_html_escape
    test_html_page_generator
    test_http_poll_server_simple
    test_security_malicious_clients
    test_udp_discovery
    test_unix_socket
    test_http_stress
    test_http_response_builder
    test_http_response_parser
    test_client_http_request
    test_bugfix_coverage
    test_http_client_server
)

# Slow tests (disabled by default, enable with -DALLOW_SLOW_TESTS=ON)
option(ALLOW_SLOW_TESTS "Build and run slow tests (e.g., test_timeout_heap)" OFF)
if(ALLOW_SLOW_TESTS)
    list(APPEND TEST_SOURCES test_timeout_heap)
    message(STATUS "Slow tests enabled: test_timeout_heap will be built")
endif()

# On macOS with Homebrew LLVM, the compiler's libc++ headers may reference
# symbols (e.g. __hash_memory) not present in Apple's system libc++.
# Detect and link against Homebrew LLVM's own libc++ when available.
if(APPLE AND NOT MSVC)
    get_filename_component(_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_LLVM_ROOT "${_COMPILER_DIR}" DIRECTORY)
    if(EXISTS "${_LLVM_ROOT}/lib/c++")
        set(_LLVM_LIBCXX_DIR "${_LLVM_ROOT}/lib/c++")
    endif()
endif()

# ---------------------------------------------------------------------------
# Shared precompiled header for all test targets.
# test_pch_host compiles test_pch.h once; every test executable reuses it via
# REUSE_FROM, avoiding 25 separate PCH compilations (saves ~5-7 s wall time).
# The compile flags on test_pch_host must exactly match those applied to the
# test executables so clang/MSVC accepts the shared PCH.
# Temporarily disabled on macOS due to PVS-Studio compatibility issues
# (consistent with lib/CMakeLists.txt).
# ---------------------------------------------------------------------------
if(NOT APPLE)
    add_library(test_pch_host STATIC EXCLUDE_FROM_ALL
        ${CMAKE_CURRENT_SOURCE_DIR}/pch_stub.cpp
    )
    target_link_libraries(test_pch_host PRIVATE aiSocksLib)
    target_include_directories(test_pch_host PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/lib/include
    )
    if(NOT MSVC)
        target_compile_options(test_pch_host PRIVATE
            $<$<CONFIG:Release>:-O1>
            $<$<CONFIG:Release>:-fno-lto>
            $<$<CONFIG:Debug>:-g>
            $<$<CONFIG:Debug>:-O0>
        )
    else()
        target_compile_options(test_pch_host PRIVATE
            /utf-8
            $<$<CONFIG:Release>:/O1>
            $<$<CONFIG:Debug>:/Zi>
            $<$<CONFIG:Debug>:/Od>
        )
    endif()
    target_precompile_headers(test_pch_host PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/test_pch.h
    )
endif()

foreach(TEST_NAME ${TEST_SOURCES})
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME} PRIVATE aiSocksLib)
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/lib/include
    )
    if(NOT APPLE)
        target_precompile_headers(${TEST_NAME} REUSE_FROM test_pch_host)
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    # Under Wine (cross-compiling), each process incurs ~10s startup overhead;
    # triple the default timeout so no test is falsely killed by ctest.
    if(CMAKE_CROSSCOMPILING)
        set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 90)
    else()
        set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 30)
    endif()
    
    # Windows: prevent min/max macro conflicts (inherited from aiSocksLib PUBLIC)
    # Already defined as PUBLIC in aiSocksLib, but explicit here for clarity

    # ── -O1 in Release for test binaries ────────────────────────────────────
    # Tests don't need -O2/-O3.  Halves the LLVM backend time per test TU
    # without affecting the library itself (which keeps its own optimisation
    # level).  GENERATOR_EXPRESSION so Debug/RelWithDebInfo are unaffected.
    if(NOT MSVC)
        target_compile_options(${TEST_NAME} PRIVATE
            $<$<CONFIG:Release>:-O1>
            $<$<CONFIG:Release>:-fno-lto>
            $<$<CONFIG:Debug>:-g>
            $<$<CONFIG:Debug>:-O0>
        )
        target_link_options(${TEST_NAME} PRIVATE
            $<$<CONFIG:Debug>:-g>
        )
    else()
        target_compile_options(${TEST_NAME} PRIVATE
            /utf-8          # treat source as UTF-8 (handles em dashes in comments)
            $<$<CONFIG:Release>:/O1>
            $<$<CONFIG:Debug>:/Zi>
            $<$<CONFIG:Debug>:/Od>
        )
        target_link_options(${TEST_NAME} PRIVATE
            $<$<CONFIG:Debug>:/DEBUG>
        )
    endif()

    # Note: UNITY_BUILD is intentionally omitted.  Each test binary has
    # exactly one .cpp, so unity merging is a no-op and only adds confusion.

    # On macOS with Homebrew LLVM: link against its libc++ so template
    # instantiations (e.g. unordered_map) resolve symbols like __hash_memory.
    if(DEFINED _LLVM_LIBCXX_DIR)
        target_link_directories(${TEST_NAME} PRIVATE "${_LLVM_LIBCXX_DIR}")
        set_target_properties(${TEST_NAME} PROPERTIES
            BUILD_RPATH "${_LLVM_LIBCXX_DIR}")
    endif()
endforeach()

# test_timeout_heap sleeps for multiple keep-alive windows across 5 test cases
if(ALLOW_SLOW_TESTS)
    if(CMAKE_CROSSCOMPILING)
        set_tests_properties(test_timeout_heap PROPERTIES TIMEOUT 180)
    else()
        set_tests_properties(test_timeout_heap PROPERTIES TIMEOUT 60)
    endif()
endif()

# Under Wine the per-process overhead is ~10s; these tests do real waiting
# on top of that, so they need larger budgets when cross-compiling.
if(CMAKE_CROSSCOMPILING)
    set_tests_properties(test_construction PROPERTIES TIMEOUT 180)
    set_tests_properties(test_tcp_socket   PROPERTIES TIMEOUT 180)
else()
    # test_construction spins up server threads; give it more room
    set_tests_properties(test_construction PROPERTIES TIMEOUT 60)
    # test_tcp_socket includes timeout tests (connect to black-hole IP)
    set_tests_properties(test_tcp_socket   PROPERTIES TIMEOUT 60)
endif()

# ---------------------------------------------------------------------------
# libFuzzer targets (opt-in: -DENABLE_FUZZER=ON, requires Clang)
# ---------------------------------------------------------------------------
option(ENABLE_FUZZER "Build libFuzzer fuzz targets (requires Clang)" OFF)
if(ENABLE_FUZZER)
    if(MSVC)
        message(FATAL_ERROR "libFuzzer is not supported with MSVC")
    endif()
    set(FUZZ_TARGETS
        fuzz_http_request   # HttpRequest::parse()         — request parser
        fuzz_http_response  # HttpResponseParser::feed()   — response/chunk parser
        fuzz_url_codec      # urlDecode / urlDecodePath    — percent-decoding
        fuzz_html_escape    # HtmlEscape::encode()         — XSS escaping
        fuzz_resolve_path   # HttpFileServer::resolveFilePath() — path traversal
        fuzz_pipeline       # parse → urlDecodePath → resolveFilePath end-to-end
        fuzz_path_helper          # PathHelper: normalizePath / isPathWithin / joinPath
        fuzz_client_url_parse     # ClientHttpRequest::parseUrl() via forUrl()
        fuzz_client_roundtrip     # forUrl()/forPost() builder + HttpRequest::parse() round-trip
    )
    # libFuzzer provides its own main(), so these are NOT added to ctest.
    foreach(FUZZ_TARGET ${FUZZ_TARGETS})
        add_executable(${FUZZ_TARGET} ${FUZZ_TARGET}.cpp)
        target_link_libraries(${FUZZ_TARGET} PRIVATE aiSocksLib)
        target_include_directories(${FUZZ_TARGET} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/lib/include
        )
        target_compile_options(${FUZZ_TARGET} PRIVATE
            -fsanitize=fuzzer,address
            -fno-omit-frame-pointer
        )
        target_link_options(${FUZZ_TARGET} PRIVATE
            -fsanitize=fuzzer,address
        )
        if(DEFINED _LLVM_LIBCXX_DIR)
            target_link_directories(${FUZZ_TARGET} PRIVATE "${_LLVM_LIBCXX_DIR}")
            set_target_properties(${FUZZ_TARGET} PROPERTIES
                BUILD_RPATH "${_LLVM_LIBCXX_DIR}")
        endif()
        message(STATUS "libFuzzer target enabled: ${FUZZ_TARGET}")
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Post-build test runner
# ---------------------------------------------------------------------------
# Custom target to run all tests on demand (not automatically on every build).
# Use: cmake --build build-ninja --target run_all_tests
# Or invoke via the "Run all tests" task in VS Code.
#
# Works on Windows (MSVC / Ninja), Linux, and macOS.
# -C $<CONFIG> is a no-op on single-config generators but required for MSVC.
# ---------------------------------------------------------------------------
# Only add test runner when building natively; skip when cross-compiling
# (the .exe binaries can't execute on a non-Windows host).
# Only add test runner when building natively; skip when cross-compiling
# (the .exe binaries can't execute on a non-Windows host).
if(NOT CMAKE_CROSSCOMPILING)
    add_custom_target(run_all_tests
        COMMAND ${CMAKE_CTEST_COMMAND}
            --output-on-failure
            --no-compress-output
            -C $<CONFIG>
            --timeout 60
            -j 11
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "========== Running aiSocks tests =========="
        DEPENDS ${TEST_SOURCES}
        VERBATIM
    )
else()
    # Cross-compiling (MinGW/Wine): use wine_test.sh which sets up a RAM-disk
    # WINEPREFIX and a persistent wineserver before handing off to ctest -j8.
    add_custom_target(run_wine_tests
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/wine_test.sh"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "========== Running aiSocks Wine tests (RAM disk + persistent wineserver) =========="
        DEPENDS ${TEST_SOURCES}
        VERBATIM
    )

    # wine --version at post-build time confirms wine is on PATH and the Wine
    # prefix is healthy.  The x86_64 wine binary runs under Rosetta on Apple
    # Silicon; this call is too short to meaningfully warm the Rosetta JIT, but
    # it catches a missing/broken wine installation before wine_test.sh runs.
    list(GET TEST_SOURCES -1 _LAST_TEST)
    find_program(_WINE_EXECUTABLE wine)
    if(_WINE_EXECUTABLE)
        add_custom_command(TARGET ${_LAST_TEST} POST_BUILD
            COMMAND "${_WINE_EXECUTABLE}" --version
            COMMENT "Checking wine is available..."
            VERBATIM
        )
    endif()
endif()
