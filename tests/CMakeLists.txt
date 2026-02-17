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
endforeach()

# test_construction spins up server threads; give it more room
set_tests_properties(test_construction PROPERTIES TIMEOUT 60)

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
