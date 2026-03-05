# Test Coverage Improvement Plan

**Created:** 2026-03-05  
**Status:** In Progress

## Overview
Comprehensive test coverage improvements for AISocks library, addressing critical gaps in security-sensitive components and core infrastructure.

---

## Task List

### 1. PathHelper Tests (HIGH PRIORITY - Security Critical) ✅
**File:** `tests/test_path_helper.cpp`  
**Status:** COMPLETED  
**Dependencies:** None  
**Functions to Test:**
- [x] `normalizePath()` - Path separator normalization
- [x] `getCanonicalPath()` - Path canonicalization (. and .. resolution)
- [x] `isPathWithin()` - Path containment checking (security-critical)
- [x] `hasSymlinkComponentWithin()` - Symlink detection (security-critical)
- [x] `getFilename()` - Filename extraction
- [x] `getExtension()` - Extension extraction
- [x] `joinPath()` - Path joining
- [x] `listDirectory()` - Directory listing
- [x] `getFileInfo()` - File metadata
- [x] `exists()`, `isDirectory()`, `isSymlink()` - File type checks

**Test Cases:**
- [x] Path traversal attempts (../, ../../, etc.)
- [x] Symlink escapes from document root
- [x] Windows vs Unix path separators
- [x] Edge cases: empty paths, ".", "..", root paths
- [x] Symlink chains and cycles
- [x] Permission denied scenarios

---

### 2. Result<T> Tests (HIGH PRIORITY) ✅
**File:** `tests/test_result.cpp`  
**Status:** COMPLETED  
**Dependencies:** None  
**Functions to Test:**
- [x] `buildMessage()` - Error message construction
- [x] Copy/move constructors with success/error states
- [x] Copy/move assignment with success/error states
- [x] `value()` - Access with precondition checks
- [x] `value_or()` - Fallback behavior
- [x] `message()` - Lazy message construction
- [x] `error()` - Error code access
- [x] `sysCode()` - System error code
- [x] Destructor correctness (placement new cleanup)
- [x] `isSuccess()`, `isError()` - State queries

**Test Cases:**
- [x] Success case: construct, copy, move, access value
- [x] Error case: construct, copy, move, access error
- [x] Lazy message construction (build only when accessed)
- [x] Invalid value() access (should assert)
- [x] Mixed copy/move operations
- [x] Result<T> with non-trivial T types

---

### 3. HttpPollServer Tests (MEDIUM PRIORITY) ✅
**File:** `tests/test_http_poll_server.cpp`  
**Status:** COMPLETED  
**Dependencies:** None (friend access not needed for current tests)  
**Functions to Test:**
- [x] Keep-alive negotiation (HTTP/1.0 vs HTTP/1.1)
- [x] Response streaming with zero-copy views
- [x] `onResponseBegin()` hook
- [x] `onResponseSent()` hook
- [x] Request buffer management and growth
- [x] Connection: keep-alive header processing
- [x] Connection: close header processing
- [x] Request scan position tracking (incremental parsing)
- [x] Response buffer vs response view switching

**Test Cases:**
- [x] HTTP/1.0 request (default close after send)
- [x] HTTP/1.1 request with explicit keep-alive
- [x] HTTP/1.1 request with explicit close
- [x] Large response (multiple send() calls)
- [x] Static response (zero-copy responseView)
- [x] Dynamic response (responseBuf ownership)
- [x] Hook timing verification

---

### 4. ChronoCompat Evaluation 🔍
**File:** `lib/include/ChronoCompat.h`  
**Status:** TO DO  
**Action Items:**
- [ ] Review usage across codebase (grep for ChronoCompat includes)
- [ ] Check if chrono is already included everywhere
- [ ] Determine if this provides actual compile-time benefit
- [ ] Decision: Keep with basic tests OR remove if redundant

**Initial Assessment:** Simple conversion utilities; if chrono is already used everywhere, this may be redundant. Will evaluate and decide.

---

### 5. Remove SocketHelpers.h (LOW PRIORITY) ✅
**File:** `lib/include/SocketHelpers.h`  
**Status:** COMPLETED  
**Dependencies:** None  
**Action Items:**
- [x] Verify file is truly empty/unused
- [x] Check for any includes of this file
- [x] Remove from Socket.cpp
- [x] Delete the file
- [x] Verify build still works

---

### 6. ServerSignal Tests (MEDIUM PRIORITY) ✅
**File:** `tests/test_server_signal.cpp`  
**Status:** COMPLETED  
**Dependencies:** None  
**Functions to Test:**
- [x] `g_serverSignalStop` flag behavior
- [x] Signal handler installation (`installSignalHandlers()`)
- [x] Signal handler cleanup
- [x] Thread-safety of signal flag access
- [x] SIGINT handling
- [x] SIGTERM handling (Unix)
- [x] Ctrl+C handling (Windows)
- [x] Multiple signal delivery

**Test Cases:**
- [x] Install handlers, verify registration
- [x] Simulate signal delivery, check flag
- [x] Multiple threads reading flag concurrently
- [x] setHandleSignals(true) vs setHandleSignals(false) interaction
- [x] Signal handler cleanup on shutdown

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

1. ✅ **PathHelper** (Security critical - DONE)
2. ✅ **Result<T>** (Core error handling - DONE)
3. ✅ **SocketHelpers.h removal** (Quick win - DONE)
4. ✅ **ServerSignal** (Shutdown correctness - DONE)
5. ✅ **HttpPollServer** (Core HTTP infrastructure - DONE)
6. ⏳ **test_advanced_file_server enhancements** (IN PROGRESS)
7. ⏳ **ServerBase gaps** (TO DO)
8. ⏳ **ChronoCompat** (TO DO - Evaluate/test last)

---

## Completed Tasks ✅

### Session 1 - 2026-03-05

1. **PathHelper Tests** ✅ - Created `test_path_helper.cpp` with 36 comprehensive tests covering:
   - normalizePath, getCanonicalPath, isPathWithin
   - Security tests for path traversal, null bytes, long paths
   - Filename/extension extraction
   - Directory operations
   - Added to CMakeLists.txt

2. **Result<T> Tests** ✅ - Created `test_result.cpp` with 35 comprehensive tests covering:
   - Success and error construction
   - Copy/move semantics for both states
   - Lazy message construction
   - value_or() fallback
   - Placement new/destructor correctness
   - Non-trivial types (std::string, std::vector, TcpSocket)
   - Added to CMakeLists.txt

3. **ServerSignal Tests** ✅ - Created `test_server_signal.cpp` with 15 comprehensive tests covering:
   - g_serverSignalStop flag manipulation
   - Thread-safety (atomic operations)
   - Server integration with handleSignals flag
   - Multiple servers sharing same flag
   - Memory ordering and stress tests
   - Added to CMakeLists.txt

4. **HttpPollServer Tests** ✅ - Created `test_http_poll_server.cpp` with 15 comprehensive tests covering:
   - Zero-copy vs dynamic response paths
   - onResponseBegin/onResponseSent hooks
   - HTTP/1.0 vs HTTP/1.1 keep-alive
   - Connection: close header
   - Request buffering and partial requests
   - Large responses
   - Multiple concurrent clients
   - Incremental parsing (request scan position)
   - Added to CMakeLists.txt

5. **SocketHelpers.h Removal** ✅ - Removed empty placeholder file:
   - Removed include from Socket.cpp
   - Deleted lib/include/SocketHelpers.h
   - Updated Plan.md

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
