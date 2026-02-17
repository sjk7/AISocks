cmake_minimum_required(VERSION 3.15)
project(aiSocks VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Always generate compile_commands.json so clangd can find headers.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS "Build test programs" ON)

# Add the library
add_subdirectory(lib)

# Enable CTest
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Add examples
if(BUILD_EXAMPLES)
    add_executable(aiSocksExample examples/main.cpp)
    target_link_libraries(aiSocksExample PRIVATE aiSocksLib)
    
    add_executable(aiSocksNonBlocking examples/nonblocking.cpp)
    target_link_libraries(aiSocksNonBlocking PRIVATE aiSocksLib)
    
    add_executable(aiSocksTestBlocking examples/test_blocking_state.cpp)
    target_link_libraries(aiSocksTestBlocking PRIVATE aiSocksLib)
    
    add_executable(aiSocksTestIPv6 examples/test_ipv6.cpp)
    target_link_libraries(aiSocksTestIPv6 PRIVATE aiSocksLib)
    
    add_executable(aiSocksTestIPUtils examples/test_ip_utils.cpp)
    target_link_libraries(aiSocksTestIPUtils PRIVATE aiSocksLib)
    
    add_executable(aiSocksTestMove examples/test_move_semantics.cpp)
    target_link_libraries(aiSocksTestMove PRIVATE aiSocksLib)

    add_executable(aiSocksPeerLogger examples/peer_logger.cpp)
    target_link_libraries(aiSocksPeerLogger PRIVATE aiSocksLib)

    # Set the startup project for Visual Studio
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT aiSocksExample)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "aiSocks Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Examples: ${BUILD_EXAMPLES}")
message(STATUS "  System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "")
