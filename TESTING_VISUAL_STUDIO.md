# Running Tests in Visual Studio

This guide explains how to run aiSocks tests in Visual Studio 2022 when opening the generated `.sln` file.

## Overview

The aiSocks test suite uses a custom lightweight test framework (not Google Test or Catch2), so Visual Studio's Test Explorer won't auto-discover individual test cases. However, you can still run all tests effectively using several methods.

---

## Method 1: RUN_TESTS Target (Easiest)

The Visual Studio solution includes an auto-generated `RUN_TESTS` project that runs all tests via CTest.

### Steps:
1. Open `build-vs\aiSocks.sln` in Visual Studio 2022
2. In **Solution Explorer**, locate the `RUN_TESTS` project (usually at the bottom)
3. Right-click `RUN_TESTS` → **Build**
4. The **Output** window will show test results

### What you'll see:
```
Test project C:/Users/cool/Desktop/AISocks/build-vs
    Start 1: test_socket_basics
1/22 Test #1: test_socket_basics ...............   Passed    0.02 sec
    Start 2: test_ip_utils
2/22 Test #2: test_ip_utils ....................   Passed    0.03 sec
...
100% tests passed, 0 tests failed out of 22
```

---

## Method 2: Run Individual Tests

Each test is a standalone executable that you can run directly.

### Steps:
1. In **Solution Explorer**, expand the `tests` folder
2. Right-click any test project (e.g., `test_tcp_socket`)
3. Select **Set as Startup Project**
4. Press **F5** (Debug) or **Ctrl+F5** (Run without debugging)

### Available Test Projects:
- `test_socket_basics` - Basic socket operations
- `test_tcp_socket` - TCP socket functionality
- `test_server_base` - Server base class
- `test_error_messages` - Error handling and messages
- `test_poller` - Polling mechanism
- `test_construction` - Object construction
- `test_error_handling` - Error handling paths
- `test_loopback_tcp` - Loopback connections
- `test_blocking` - Blocking mode behavior
- `test_move_semantics` - Move operations
- `test_new_features` - New feature tests
- `test_simple_client` - Simple client tests
- `test_http_request` - HTTP request parsing
- `test_url_codec` - URL encoding/decoding
- `test_socket_factory` - Socket factory pattern
- `test_server_base_simple` - Simple server tests
- `test_server_base_minimal` - Minimal server tests
- `test_server_base_echo_simple` - Echo server tests
- `test_server_base_no_timeout` - No-timeout server tests
- `test_fixes` - Bug fix verification
- `test_simple_server` - Simple server implementation

### Test Output:
Each test prints results to the console:
```
  pass: srv.isValid()
  pass: srv.bind("127.0.0.1", Port::any)
  pass: srv.listen(1)
...
=== Test Summary ===
Passed: 45
Failed: 0
```

---

## Method 3: Developer Command Prompt

Run tests from Visual Studio's Developer Command Prompt or Terminal.

### Steps:
1. In Visual Studio: **View → Terminal** (or `Ctrl+``)
2. Navigate to the build directory:
   ```powershell
   cd build-vs
   ```
3. Run all tests:
   ```powershell
   ctest -C Debug --output-on-failure
   ```
4. Or run a specific test:
   ```powershell
   .\tests\Debug\test_tcp_socket.exe
   ```

### Useful CTest Options:
```powershell
# Run tests in parallel (4 jobs)
ctest -C Debug -j 4

# Run only tests matching a pattern
ctest -C Debug -R "server_base"

# Show all test output (even passing tests)
ctest -C Debug -V

# List all available tests
ctest -C Debug --show-only
```

---

## Method 4: Custom Build Target (run_all_tests)

The solution includes a custom `run_all_tests` target for convenience.

### Steps:
1. In **Solution Explorer**, locate `run_all_tests` (custom target)
2. Right-click `run_all_tests` → **Build**
3. Tests run with parallel execution and detailed output

---

## Debugging Individual Tests

To debug a failing test:

### Steps:
1. Right-click the test project → **Set as Startup Project**
2. Set breakpoints in the test source file (in `tests/` folder)
3. Press **F5** to start debugging
4. Visual Studio will stop at your breakpoints

### Example:
To debug `test_tcp_socket`:
1. Open `tests\test_tcp_socket.cpp`
2. Set a breakpoint on a specific test (e.g., line with `BEGIN_TEST`)
3. Right-click `test_tcp_socket` → **Set as Startup Project**
4. Press **F5**

---

## Understanding Test Results

### Exit Codes:
- **0** = All tests passed
- **1** = One or more tests failed

### Test Output Format:
```
  pass: socket.isValid()
  pass: socket.connect("127.0.0.1", Port{8080})
  FAIL [test_tcp_socket.cpp:123] socket.send(data, size) > 0
```

Each test prints:
- `pass:` for successful assertions
- `FAIL` with file/line for failures
- Summary at the end with pass/fail counts

---

## Build Configurations

Tests are available in both Debug and Release configurations:

### Debug (default):
```powershell
ctest -C Debug --output-on-failure
```

### Release:
```powershell
# First build Release configuration
cmake --build build-vs --config Release
# Then run tests
ctest -C Release --output-on-failure
```

---

## Test Timeouts

Tests have timeouts to prevent hangs:
- Default: 30 seconds per test
- `test_construction`: 60 seconds (spins up server threads)
- `test_tcp_socket`: 60 seconds (includes timeout tests)
- `test_timeout_heap`: 60 seconds (only if `ALLOW_SLOW_TESTS=ON`)

If a test times out, CTest will terminate it and mark it as failed.

---

## Slow Tests

The `test_timeout_heap` test is disabled by default (takes ~20 seconds).

### To enable slow tests:
```powershell
# Reconfigure with slow tests enabled
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_TESTS=ON -DALLOW_SLOW_TESTS=ON

# Rebuild
cmake --build build-vs --config Debug

# Run all tests (including slow ones)
ctest -C Debug --output-on-failure
```

---

## Troubleshooting

### "0 tests found" in Test Explorer
Visual Studio's Test Explorer only discovers tests from recognized frameworks (Google Test, MSTest, Catch2, etc.). The aiSocks tests use a custom framework, so they won't appear in Test Explorer. Use the `RUN_TESTS` target or run tests individually instead.

### Tests hang or timeout
- Check if a server test is waiting for a client connection
- Ensure no other process is using the test ports
- Try running the test individually to see detailed output

### Build errors
- Ensure you've selected the correct configuration (Debug/Release)
- Clean and rebuild: **Build → Clean Solution**, then **Build → Rebuild Solution**

### Port conflicts
Tests use dynamic port allocation (`Port::any`) to avoid conflicts. If you see bind errors, another process may be holding a port. Restart Visual Studio or reboot.

---

## Quick Reference

| Task | Method |
|------|--------|
| Run all tests | Right-click `RUN_TESTS` → Build |
| Run one test | Right-click test project → Set as Startup → F5 |
| Debug a test | Set breakpoints → Set as Startup → F5 |
| Command line | `ctest -C Debug --output-on-failure` |
| List tests | `ctest -C Debug --show-only` |
| Parallel run | `ctest -C Debug -j 4` |

---

## Test Suite Summary

**Total Tests:** 22  
**Average Runtime:** ~8-9 seconds (all tests)  
**Pass Rate:** 100% (as of last run)

**Slowest Tests:**
- `test_error_messages` (~2.3s) - DNS and connection timeouts
- `test_fixes` (~1.0s) - Bug fix verification
- `test_simple_client` (~0.8s) - Client connection tests
- `test_tcp_socket` (~0.7s) - Comprehensive TCP tests

**Fastest Tests:**
- `test_socket_basics` (~0.02s)
- `test_url_codec` (~0.01s)
- `test_http_request` (~0.03s)
