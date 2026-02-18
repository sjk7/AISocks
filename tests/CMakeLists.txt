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
# Runs every test automatically after each successful compile.
# The build fails (non-zero exit) if any test fails, and the failing test's
# full output is printed so the problem is immediately visible.
#
# Works on Windows (MSVC / Ninja), Linux, and macOS.
# -C $<CONFIG> is a no-op on single-config generators but required for MSVC.
# ---------------------------------------------------------------------------
add_custom_target(run_all_tests ALL
    COMMAND ${CMAKE_CTEST_COMMAND}
        --output-on-failure
        --no-compress-output
        -C $<CONFIG>
        --timeout 60
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "========== Running aiSocks tests =========="
    DEPENDS ${TEST_SOURCES}
    VERBATIM
)
