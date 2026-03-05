# Test Coverage Improvement Plan

**Created:** 2026-03-05  
**Status:** In Progress

## Overview
Comprehensive test coverage improvements for AISocks library, addressing critical gaps in security-sensitive components and core infrastructure.

---

## Task List

### 1. PathHelper Tests (HIGH PRIORITY - Security Critical) ⏳
**File:** `tests/test_path_helper.cpp`  
**Status:** Not Started  
**Dependencies:** None  
**Functions to Test:**
- [ ] `normalizePath()` - Path separator normalization
- [ ] `getCanonicalPath()` - Path canonicalization (. and .. resolution)
- [ ] `isPathWithin()` - Path containment checking (security-critical)
- [ ] `hasSymlinkComponentWithin()` - Symlink detection (security-critical)
- [ ] `getFilename()` - Filename extraction
- [ ] `getExtension()` - Extension extraction
- [ ] `joinPath()` - Path joining
- [ ] `listDirectory()` - Directory listing
- [ ] `getFileInfo()` - File metadata
- [ ] `exists()`, `isDirectory()`, `isSymlink()` - File type checks

**Test Cases:**
- Path traversal attempts (../, ../../, etc.)
- Symlink escapes from document root
- Windows vs Unix path separators
- Edge cases: empty paths, ".", "..", root paths
- Symlink chains and cycles
- Permission denied scenarios

---

### 2. Result<T> Tests (HIGH PRIORITY) ⏳
**File:** `tests/test_result.cpp`  
**Status:** Not Started  
**Dependencies:** None  
**Functions to Test:**
- [ ] `buildMessage()` - Error message construction
- [ ] Copy/move constructors with success/error states
- [ ] Copy/move assignment with success/error states
- [ ] `value()` - Access with precondition checks
- [ ] `value_or()` - Fallback behavior
- [ ] `message()` - Lazy message construction
- [ ] `error()` - Error code access
- [ ] `sysCode()` - System error code
- [ ] Destructor correctness (placement new cleanup)
- [ ] `isSuccess()`, `isError()` - State queries

**Test Cases:**
- Success case: construct, copy, move, access value
- Error case: construct, copy, move, access error
- Lazy message construction (build only when accessed)
- Invalid value() access (should assert)
- Mixed copy/move operations
- Result<T> with non-trivial T types

---

### 3. HttpPollServer Tests (MEDIUM PRIORITY) ⏳
**File:** `tests/test_http_poll_server.cpp`  
**Status:** Not Started  
**Dependencies:** Need friend access to HttpClientState internals  
**Functions to Test:**
- [ ] Keep-alive negotiation (HTTP/1.0 vs HTTP/1.1)
- [ ] Response streaming with zero-copy views
- [ ] `onResponseBegin()` hook
- [ ] `onResponseSent()` hook
- [ ] Request buffer management and growth
- [ ] Connection: keep-alive header processing
- [ ] Connection: close header processing
- [ ] Request scan position tracking (incremental parsing)
- [ ] Response buffer vs response view switching

**Test Cases:**
- HTTP/1.0 request (default close after send)
- HTTP/1.1 request with explicit keep-alive
- HTTP/1.1 request with explicit close
- Large response (multiple send() calls)
- Static response (zero-copy responseView)
- Dynamic response (responseBuf ownership)
- Hook timing verification

**Friend Declarations Needed:**
- Friend test class in HttpPollServer to access HttpClientState internals

---

### 4. ChronoCompat Evaluation 🔍
**File:** `lib/include/ChronoCompat.h`  
**Status:** Not Started  
**Action Items:**
- [ ] Review usage across codebase (grep for ChronoCompat includes)
- [ ] Check if chrono is already included everywhere
- [ ] Determine if this provides actual compile-time benefit
- [ ] Decision: Keep with basic tests OR remove if redundant

**Initial Assessment:** Simple conversion utilities; if chrono is already used everywhere, this may be redundant. Will evaluate and decide.

---

### 5. Remove SocketHelpers.h (LOW PRIORITY) ⏳
**File:** `lib/include/SocketHelpers.h`  
**Status:** Not Started  
**Dependencies:** None  
**Action Items:**
- [ ] Verify file is truly empty/unused
- [ ] Check for any includes of this file
- [ ] Remove from CMakeLists.txt if present
- [ ] Delete the file
- [ ] Verify build still works

---

### 6. ServerSignal Tests (MEDIUM PRIORITY) ⏳
**File:** `tests/test_server_signal.cpp`  
**Status:** Not Started  
**Dependencies:** None  
**Functions to Test:**
- [ ] `g_serverSignalStop` flag behavior
- [ ] Signal handler installation (`installSignalHandlers()`)
- [ ] Signal handler cleanup
- [ ] Thread-safety of signal flag access
- [ ] SIGINT handling
- [ ] SIGTERM handling (Unix)
- [ ] Ctrl+C handling (Windows)
- [ ] Multiple signal delivery

**Test Cases:**
- Install handlers, verify registration
- Simulate signal delivery, check flag
- Multiple threads reading flag concurrently
- setHandleSignals(true) vs setHandleSignals(false) interaction
- Signal handler cleanup on shutdown

**Implementation Notes:**
- Cannot actually send real signals in unit test (would kill test process)
- Test by directly calling handler functions or manipulating flag
- Verify atomic flag operations are correct

---

### 7. test_advanced_file_server.cpp Enhancements (MEDIUM PRIORITY) ⏳
**File:** `tests/test_advanced_file_server.cpp`  
**Status:** Partially Complete  
**Missing Tests:**
- [ ] HEAD request method (verify no body in response)
- [ ] Directory listing feature (if enabled in config)
- [ ] Range requests (partial content, HTTP 206)
- [ ] If-Modified-Since header handling
- [ ] If-None-Match header handling (ETag)
- [ ] Custom MIME types (`.wasm`, `.ts`, `.jsx`, `.tsx`)
- [ ] Concurrent client connections
- [ ] File caching behavior (verify cache hits/misses)
- [ ] Large file streaming
- [ ] Byte range boundaries and errors

**Test Cases:**
- HEAD /index.html - verify 200 with headers but no body
- GET with Range: bytes=0-99 - verify 206 partial content
- GET with If-Modified-Since (fresh file) - verify 304 Not Modified
- GET with If-Modified-Since (stale file) - verify 200 with content
- GET /file.wasm - verify Content-Type: application/wasm
- Concurrent 10 clients requesting different files
- Second request for same file hits cache

---

### 8. ServerBase Coverage Gaps (MEDIUM PRIORITY) ⏳
**File:** `tests/test_server_base.cpp` (enhance existing)  
**Status:** Partially Complete  
**Missing Tests:**
- [ ] Timeout interaction with high client load
- [ ] Client limit with rapid connect/disconnect cycles
- [ ] onDisconnect() return value handling
- [ ] Error during accept() handling
- [ ] Error during poll wait handling
- [ ] Maximum connections edge case (exactly at limit)
- [ ] Server destruction while clients connected
- [ ] Poll timeout accuracy under load

**Test Cases:**
- 100 clients rapidly connecting, some timing out
- Client limit reached, new connection rejected
- onDisconnect returns StopServer, verify server stops
- Simulate accept() failure (hard to test, may need mock)
- Server destructor called with active connections
- Timeout sweep with 1000+ entries (stress test)

**Friend Declarations Needed:**
- May need friend access to timeout_heap_ for verification

---

## Implementation Order (Priority-Driven)

1. **PathHelper** (Security critical - do first)
2. **Result<T>** (Core error handling)
3. **SocketHelpers.h removal** (Quick win)
4. **ServerSignal** (Shutdown correctness)
5. **HttpPollServer** (Core HTTP infrastructure)
6. **test_advanced_file_server enhancements** (Complete existing tests)
7. **ServerBase gaps** (Enhance existing coverage)
8. **ChronoCompat** (Evaluate/test last)

---

## Completed Tasks ✅

_None yet_

---

## Notes

- All tests should follow existing patterns in tests/test_*.cpp
- Use BEGIN_TEST() and REQUIRE() macros from test_helpers.h
- Friend declarations: Add `friend class Test_ClassName;` in class declarations
- Keep tests focused and isolated (no interdependencies)
- Each test file should have its own main() and exit code

---

## Build/Test Commands

```bash
# Build all tests
cmake --build build-mac --parallel 8

# Run all tests
cd build-mac && ctest --output-on-failure --parallel 4

# Run specific test
./build-mac/tests/test_path_helper
```

---

## Session History

### Session 1 - 2026-03-05
- Created initial Plan.md
- Analyzed test coverage gaps
- Identified 8 major task areas
- Ready to begin implementation
