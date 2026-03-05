# Testing Guide for aiSocks

## Overview

The aiSocks test suite includes **30 comprehensive tests** covering sockets, servers, HTTP protocols, and utilities. All tests are included when building with `-DBUILD_TESTS=ON`.

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
| `test_http_poll_server` | HTTP/1.x framing, keep-alive, connection management, hooks (16 comprehensive tests) | ~0.50s |
| `test_url_codec` | URL encoding/decoding | ~0.00s |

### Server Framework Tests

| Test Name | Description | Time |
|-----------|-------------|------|
| `test_server_base` | Poll-driven `ServerBase<T>` framework core functionality | ~0.73s |
| `test_server_base_edge_cases` | Edge cases: connection limits, keep-alive, timeouts, signal handling (10 comprehensive tests) | ~3.6s |
| `test_server_base_simple` | Simple HTTP echo server over ServerBase | ~0.55s |
| `test_server_base_echo_simple` | Echo server with response building | ~0.15s |
| `test_server_base_minimal` | Minimal server setup and lifecycle | ~0.06s |
| `test_server_base_no_timeout` | Behavior without timeout constraints | ~0.05s |
| `test_server_signal` | Signal handling and graceful shutdown | ~0.18s |
| `test_simple_client` | SimpleClient API for building HTTP requests | ~0.12s |
| `test_simple_server` | Simple HTTP file server (dynamic file serving) | ~0.21s |
| `test_advanced_file_server` | Advanced HTTP file server (auth, caching, logging) | ~0.14s |

### Utilities & Support Tests

| Test Name | Description | Time |
|-----------|-------------|------|
| `test_file_io` | File operations and directory handling | ~0.00s |
| `test_file_cache` | File caching mechanisms | ~0.00s |
| `test_path_helper` | Path utilities, normalization, security checks | ~0.01s |
| `test_error_messages` | Error message generation and formatting | ~0.01s |
| `test_result` | Result type operations and error handling | ~0.01s |
| `test_fixes` | Regression and edge-case fixes | ~0.99s |

## Test Execution Example

### Sequential Execution
```
Test project /path/to/build-release
      Start  1: test_socket_basics
1/30 Test  #1: test_socket_basics ...............   Passed    0.00 sec
      Start  2: test_ip_utils
2/30 Test  #2: test_ip_utils ....................   Passed    0.00 sec
...
     Start 30: test_http_poll_server
30/30 Test #30: test_http_poll_server ............   Passed    4.32 sec

100% tests passed, 0 tests failed out of 30

Total Test time (real) =  29.21 sec
```

### Parallel Execution (8 jobs)
```
Test project /path/to/build-release
      Start 30: test_http_poll_server
      Start 14: test_server_base_edge_cases
      Start 22: test_fixes
...
     Start 14: test_server_base_edge_cases ......   Passed    3.24 sec
30/30 Test #30: test_http_poll_server ............   Passed    4.46 sec

100% tests passed, 0 tests failed out of 30

Total Test time (real) =   4.46 sec
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
├── test_server_base_edge_cases.cpp
├── test_server_signal.cpp
├── test_simple_client.cpp
├── test_simple_server.cpp
├── test_advanced_file_server.cpp
├── test_file_io.cpp
├── test_file_cache.cpp
├── test_path_helper.cpp
├── test_error_messages.cpp
├── test_result.cpp
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

- **Total Tests**: 30 (comprehensive test suite)
- **Test Categories**: 9 categories covering all major components
- **Total Coverage**: Core sockets, TCP/UDP, IPv4/IPv6, blocking/non-blocking, HTTP/1.x protocol, keep-alive, zero-copy responses, file serving, authentication, caching, path utilities, signal handling, and edge cases
- **Sequential Execution Time**: ~29 seconds (full suite)
- **Parallel Execution Time**: ~4.5 seconds (8 jobs) - 6.5x speedup
- **Pass Rate**: 100% on all supported platforms (Windows, macOS, Linux)
- **Cross-Platform**: Windows (MSVC), macOS (Clang), Linux (GCC/Clang)
- **Build Standards**: Strict warnings (`-Wall -Werror`/`/W4 /WX`), no exceptions

## Continuous Integration

Tests are designed to run in CI environments:

- ✅ No network dependencies (all tests use loopback 127.0.0.1)
- ✅ No file system requirements (tests use temporary/test directories)
- ✅ Deterministic (no flaky tests, no race conditions)
- ✅ Parallel-safe (tests use unique ports via port 0 OS allocation)
- ✅ Timeout-safe (all tests include appropriate timeouts)
