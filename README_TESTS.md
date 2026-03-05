# Testing Guide for aiSocks

## Overview

The aiSocks test suite includes **25+ comprehensive tests** covering sockets, servers, HTTP protocols, and utilities. All tests are included when building with `-DBUILD_TESTS=ON`.

## Building with Tests

### Release with Debug Info (Recommended)

#### macOS (Clang)
```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build-release --parallel
```

#### Linux (GCC or Clang)
```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build-release --parallel
```

#### Windows (MSVC)
```powershell
cmake -S . -B build-release -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build-release --config RelWithDebInfo --parallel
```

## Running Tests

### All Tests
```bash
# Linux/macOS
cd build-release
ctest --output-on-failure

# Windows
cd build-release
ctest --output-on-failure -C RelWithDebInfo
```

### Parallel Execution
```bash
# Run with 4 parallel jobs
ctest --output-on-failure -j 4

# Windows
ctest --output-on-failure -j 4 -C RelWithDebInfo
```

### Specific Test
```bash
# Run only TCP socket tests
ctest --output-on-failure -R "test_tcp_socket"

# Run all HTTP-related tests
ctest --output-on-failure -R "http|server_base"

# Windows
ctest --output-on-failure -R "test_tcp_socket" -C RelWithDebInfo
```

### List Available Tests
```bash
ctest --show-only

# Windows
ctest --show-only -C RelWithDebInfo
```

### Output Verbosity
```bash
# Show detailed output (useful for debugging)
ctest --output-on-failure -V

# Show only failed test output
ctest --output-on-failure
```

## Test Categories

### Core Socket & Network Tests

| Test Name | Description | Time |
|-----------|-------------|------|
| `test_socket_basics` | Basic socket creation, validity, and state management | ~0.00s |
| `test_tcp_socket` | TCP socket connection, data transfer, and timeouts | ~0.07s |
| `test_blocking` | Blocking and non-blocking mode behavior | ~0.04s |
| `test_loopback_tcp` | End-to-end TCP send/receive over loopback interface | ~0.08s |
| `test_ip_utils` | IPv4/IPv6 address utilities and parsing | ~0.00s |
| `test_error_handling` | Error codes, error messages, and edge cases | ~0.00s |
| `test_socket_factory` | Socket factory API for creating TCP/UDP sockets | ~0.02s |

### Object Semantics & Construction Tests

| Test Name | Description | Time |
|-----------|-------------|------|
| `test_move_semantics` | Move constructors and assignment operators | ~0.00s |
| `test_construction` | Object construction, initialization, and validity | ~0.09s |
| `test_new_features` | Build-time feature detection and compatibility | ~0.25s |

### Poll & I/O Multiplexing Tests

| Test Name | Description | Time |
|-----------|-------------|------|
| `test_poller` | Poller event registration, readiness detection, timeout handling | ~0.05s |

### HTTP Protocol Tests

| Test Name | Description | Time |
|-----------|-------------|------|
| `test_http_request` | HTTP request parsing, method/path/header extraction | ~0.00s |
| `test_http_poll_server` | HTTP/1.x framing, keep-alive, connection management, hooks (15 comprehensive tests) | ~0.50s |
| `test_url_codec` | URL encoding/decoding | ~0.00s |

### Server Framework Tests

| Test Name | Description | Time |
|-----------|-------------|------|
| `test_server_base` | Poll-driven `ServerBase<T>` framework core functionality | ~0.73s |
| `test_server_base_simple` | Simple HTTP echo server over ServerBase | ~0.55s |
| `test_server_base_echo_simple` | Echo server with response building | ~0.15s |
| `test_server_base_minimal` | Minimal server setup and lifecycle | ~0.06s |
| `test_server_base_no_timeout` | Behavior without timeout constraints | ~0.05s |
| `test_simple_client` | SimpleClient API for building HTTP requests | ~0.12s |
| `test_simple_server` | Simple HTTP file server (dynamic file serving) | ~0.21s |
| `test_advanced_file_server` | Advanced HTTP file server (auth, caching, logging) | ~0.14s |

### Utilities & Support Tests

| Test Name | Description | Time |
|-----------|-------------|------|
| `test_file_io` | File operations and directory handling | ~0.00s |
| `test_file_cache` | File caching mechanisms | ~0.00s |
| `test_error_messages` | Error message generation and formatting | ~0.01s |
| `test_fixes` | Regression and edge-case fixes | ~0.99s |

## Test Execution Example

```
Test project /path/to/build-release
      Start  1: test_socket_basics
1/25 Test  #1: test_socket_basics ...............   Passed    0.00 sec
      Start  2: test_ip_utils
2/25 Test  #2: test_ip_utils ....................   Passed    0.00 sec
      Start  3: test_blocking
3/25 Test  #3: test_blocking ....................   Passed    0.04 sec
      Start  4: test_move_semantics
4/25 Test  #4: test_move_semantics ..............   Passed    0.00 sec
      Start  5: test_loopback_tcp
5/25 Test  #5: test_loopback_tcp ................   Passed    0.08 sec
      Start  6: test_error_handling
6/25 Test  #6: test_error_handling ..............   Passed    0.00 sec
      Start  7: test_construction
7/25 Test  #7: test_construction ................   Passed    0.09 sec
      Start  8: test_new_features
8/25 Test  #8: test_new_features ................   Passed    0.25 sec
      Start  9: test_error_messages
9/25 Test  #9: test_error_messages ..............   Passed    0.01 sec
     Start 10: test_poller
10/25 Test #10: test_poller ......................   Passed    0.05 sec
     Start 11: test_tcp_socket
11/25 Test #11: test_tcp_socket ..................   Passed    0.07 sec
     Start 12: test_simple_client
12/25 Test #12: test_simple_client ...............   Passed    0.12 sec
     Start 13: test_server_base
13/25 Test #13: test_server_base .................   Passed    0.73 sec
     Start 14: test_http_request
14/25 Test #14: test_http_request ................   Passed    0.00 sec
     Start 15: test_url_codec
15/25 Test #15: test_url_codec ...................   Passed    0.00 sec
     Start 16: test_socket_factory
16/25 Test #16: test_socket_factory ..............   Passed    0.02 sec
     Start 17: test_server_base_simple
17/25 Test #17: test_server_base_simple ..........   Passed    0.55 sec
     Start 18: test_server_base_minimal
18/25 Test #18: test_server_base_minimal .........   Passed    0.06 sec
     Start 19: test_server_base_echo_simple
19/25 Test #19: test_server_base_echo_simple .....   Passed    0.15 sec
     Start 20: test_server_base_no_timeout
20/25 Test #20: test_server_base_no_timeout ......   Passed    0.05 sec
     Start 21: test_fixes
21/25 Test #21: test_fixes .......................   Passed    0.99 sec
     Start 22: test_simple_server
22/25 Test #22: test_simple_server ...............   Passed    0.21 sec
     Start 23: test_advanced_file_server
23/25 Test #23: test_advanced_file_server ........   Passed    0.14 sec
     Start 24: test_file_io
24/25 Test #24: test_file_io .....................   Passed    0.00 sec
     Start 25: test_file_cache
25/25 Test #25: test_file_cache ..................   Passed    0.00 sec

100% tests passed, 0 tests failed out of 25

Total Test time (real) =   5.12 sec
```

## Advanced Test Options

### Slow Tests

The `test_timeout_heap` test is disabled by default (takes ~20 seconds). Enable with:

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DALLOW_SLOW_TESTS=ON
cmake --build build-release --parallel
ctest --output-on-failure
```

### Test with AddressSanitizer

Detect memory bugs and write-after-free issues:

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DBUILD_TESTS=ON
cmake --build build-asan --parallel
cd build-asan
ctest --output-on-failure
```

### Test with MemorySanitizer

Detect uninitialized memory reads (Linux only):

```bash
cmake -S . -B build-msan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_MSAN=ON -DBUILD_TESTS=ON
cmake --build build-msan --parallel
cd build-msan
ctest --output-on-failure
```

### Test with UndefinedBehaviorSanitizer

Detect undefined behavior:

```bash
cmake -S . -B build-ubsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_UBSAN=ON -DBUILD_TESTS=ON
cmake --build build-ubsan --parallel
cd build-ubsan
ctest --output-on-failure
```

## Test Development

### Individual Test Files

Test source files are located in `tests/`:

```
tests/
├── test_socket_basics.cpp
├── test_blocking.cpp
├── test_tcp_socket.cpp
├── test_loopback_tcp.cpp
├── test_ip_utils.cpp
├── test_error_handling.cpp
├── test_socket_factory.cpp
├── test_move_semantics.cpp
├── test_construction.cpp
├── test_new_features.cpp
├── test_poller.cpp
├── test_http_request.cpp
├── test_http_poll_server.cpp
├── test_url_codec.cpp
├── test_server_base.cpp
├── test_server_base_simple.cpp
├── test_server_base_echo_simple.cpp
├── test_server_base_minimal.cpp
├── test_server_base_no_timeout.cpp
├── test_simple_client.cpp
├── test_simple_server.cpp
├── test_advanced_file_server.cpp
├── test_file_io.cpp
├── test_file_cache.cpp
├── test_error_messages.cpp
├── test_fixes.cpp
├── test_helpers.h          # Test utility macros (REQUIRE, BEGIN_TEST, etc.)
└── CMakeLists.txt          # Test build configuration
```

### Adding New Tests

1. Create `tests/test_my_feature.cpp` with:
```cpp
#include "test_helpers.h"
#include <iostream>

int main() {
    std::cout << "=== My Feature Tests ===\n";
    
    BEGIN_TEST("Test description") {
        REQUIRE(someCondition);
    }
    
    return test_summary();
}
```

2. Register in `tests/CMakeLists.txt`:
```cmake
add_test_executable(test_my_feature)
```

3. Rebuild and run:
```bash
cmake --build build-release --parallel
ctest --output-on-failure -R "test_my_feature"
```

## Test Metrics

- **Total Tests**: 25+
- **Total Coverage**: Core sockets, TCP/UDP, IPv4/IPv6, blocking/non-blocking, HTTP/1.x protocol, keep-alive, zero-copy responses, file serving, authentication, and caching
- **Average Execution Time**: ~5 seconds (full suite)
- **Pass Rate**: 100% on all supported platforms (Windows, macOS, Linux)

## Continuous Integration

Tests are designed to run in CI environments:

- ✅ No network dependencies (all tests use loopback 127.0.0.1)
- ✅ No file system requirements (tests use temporary/test directories)
- ✅ Deterministic (no flaky tests, no race conditions)
- ✅ Parallel-safe (tests use unique ports via port 0 OS allocation)
- ✅ Timeout-safe (all tests include appropriate timeouts)
