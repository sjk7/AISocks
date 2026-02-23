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
    test_socket_factory
    test_server_base_simple
    test_server_base_minimal
)

foreach(TEST_NAME ${TEST_SOURCES})
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME} PRIVATE aiSocksLib)
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/lib/include
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 30)
    
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
        )
    else()
        target_compile_options(${TEST_NAME} PRIVATE
            /utf-8          # treat source as UTF-8 (handles em dashes in comments)
            $<$<CONFIG:Release>:/O1>
        )
    endif()

    # Note: UNITY_BUILD is intentionally omitted.  Each test binary has
    # exactly one .cpp, so unity merging is a no-op and only adds confusion.
endforeach()

# test_construction spins up server threads; give it more room
set_tests_properties(test_construction PROPERTIES TIMEOUT 60)

# test_tcp_socket includes timeout tests (connect to black-hole IP)
set_tests_properties(test_tcp_socket PROPERTIES TIMEOUT 60)

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
endif()
