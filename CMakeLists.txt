cmake_minimum_required(VERSION 3.15)
project(aiSocks VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BUILD_EXAMPLES "Build example programs" ON)

# Add the library
add_subdirectory(lib)

# Add examples
if(BUILD_EXAMPLES)
    add_executable(aiSocksExample examples/main.cpp)
    target_link_libraries(aiSocksExample PRIVATE aiSocksLib)
    
    add_executable(aiSocksNonBlocking examples/nonblocking.cpp)
    target_link_libraries(aiSocksNonBlocking PRIVATE aiSocksLib)
    
    add_executable(aiSocksTestBlocking examples/test_blocking_state.cpp)
    target_link_libraries(aiSocksTestBlocking PRIVATE aiSocksLib)
    
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
