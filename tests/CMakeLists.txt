set(TEST_SOURCES
    test_socket_basics
    test_ip_utils
    test_blocking
    test_move_semantics
    test_loopback_tcp
    test_error_handling
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
