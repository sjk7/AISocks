# Refactoring Candidates

Functions identified as long, overly complex, or hard to reason about. Sorted by estimated severity, based on line count, cyclomatic complexity, nesting depth, and number of responsibilities.

---

## Critical

### `SocketImpl::connect()`
**File**: [lib/src/SocketImpl.cpp](lib/src/SocketImpl.cpp#L253) — lines 253–489 (~237 lines)

- Five platform branches (`#ifdef _WIN32`, `#elif __APPLE__`, `#elif __linux__`, etc.) within a single function body
- Inline RAII guard structs defined mid-function to restore blocking mode
- Deadline loop with interleaved platform-specific event-wait calls (`WSAPoll`, `kqueue`, `epoll_wait`)
- Multiple temporal phases: DNS resolution → non-blocking connect → event wait → `SO_ERROR` check → restore blocking mode
- EINTR handling duplicated per platform branch
- **Suggestion**: extract `waitForConnectWindows()`, `waitForConnectKqueue()`, `waitForConnectEpoll()` helpers; each can own its timeout loop and error translation independently

---

### `ServerBase<ClientData>::run()`
**File**: [lib/include/ServerBase.h](lib/include/ServerBase.h#L180) — lines 180–370 (~190 lines)

- Three large lambdas (`EventHandler`, `AfterBatchFn`, `StopPredicate`) with heavy captures defined and used inline
- Nesting depth reaches 6+ levels inside `EventHandler`
- State machine that manages: client acceptance, per-client event dispatch, keep-alive timeout, graceful shutdown, and signal handling — all in one body
- Understanding signal flow between the three lambdas requires building a full mental model of the event loop
- **Suggestion**: promote the three lambdas to private named member functions; consider a dedicated `ClientEventLoop` helper class for the per-client lifecycle

---

### `HttpRequest::parse()`
**File**: [lib/src/HttpRequest.cpp](lib/src/HttpRequest.cpp#L44) — lines 44–188 (~145 lines)

- Five distinct parsing phases back to back with no named boundaries: separator search → request-line parsing → header parsing → query-string extraction → query-parameter decoding
- Heavy use of `find()`, `substr()`, and `string_view` with many raw offset calculations — high off-by-one risk
- Character-by-character loops for case folding and whitespace trimming scattered throughout
- **Suggestion**: split into `parseRequestLine()`, `parseHeaders()`, `parseQueryString()`, `decodeQueryParams()`; keep `parse()` as coordinator

---

### `HttpResponseParser::tryParseHeaders_()`
**File**: [lib/src/HttpResponse.cpp](lib/src/HttpResponse.cpp#L67) — lines 67–178 (~111 lines)

- Four implicit parsing stages (status/request line → header lines → body-mode decision → chunked vs. content-length) with no named transitions
- Incremental scan for `\r\n\r\n` with mutable position state makes forward-reading difficult
- State-machine transitions are implicit assignments to `state_` fields rather than explicit control flow
- Error conditions set `error_` in multiple scattered locations
- **Suggestion**: split into `parseStatusLine_()`, `parseHeaderSection_()`, `determineBodyMode_()`

---

### `HttpResponseParser::processChunked_()`
**File**: [lib/src/HttpResponse.cpp](lib/src/HttpResponse.cpp#L245) — lines 245–340+ (~95 lines)

- Outer `while` loop with multiple inner conditionals forming a hidden state machine
- Inline hex-digit parsing with manual digit validation and accumulation
- Tracks `chunkScanPos_`, accumulated size, decoded buffers, and terminal-chunk detection all within the same loop body
- **Suggestion**: extract `parseChunkSize_(std::string_view)` → returns `std::optional<size_t>`; reduces the loop body to chunk-boundary bookkeeping only

---

### `SocketImplHelpers::getLocalAddresses()`
**File**: [lib/src/SocketImplHelpers.cpp](lib/src/SocketImplHelpers.cpp#L161) — lines 161–280 (~119 lines)

- ~50% of the function is a near-identical Windows implementation using `GetAdaptersAddresses` with `malloc`/retry loop, followed by a Unix implementation using `getifaddrs`
- Each branch has its own IPv4/IPv6 address-extraction loop with different struct layouts
- Memory management: Windows uses `malloc`/`free` with an overflow-retry loop; Unix uses RAII-ish cleanup
- **Suggestion**: extract `getLocalAddressesWindows_()` and `getLocalAddressesUnix_()`, eliminating the dual-code layout

---

### `SocketImplHelpers::resolveToSockaddr()`
**File**: [lib/src/SocketImplHelpers.cpp](lib/src/SocketImplHelpers.cpp#L61) — lines 61–130 (~70 lines)

- Two top-level branches (IPv6 vs IPv4), each with four sub-paths (wildcard, literal parse, DNS lookup, error)
- Platform conditionals inside the parameter handling (`#ifdef _WIN32`) add another layer
- Returns different error codes per code path; DNS error handling only present on non-Windows
- `memset`/`memcpy` with different sizes per branch — subtle correctness risk if sizes drift
- **Suggestion**: extract `resolveIPv4_()` and `resolveIPv6_()` helpers; centralise error-code translation

---

### `PathHelper::hasSymlinkComponentWithin()`
**File**: [lib/src/PathHelper.cpp](lib/src/PathHelper.cpp#L100) — lines 100–165 (~66 lines)

- `while` loop nested 4-5 levels deep with simultaneous tracking of `i`, `j`, `current`, relative-path offset, and a first-component flag
- Multiple `find()`-based path-component extractions within the loop are each individually off-by-one-prone
- Defensive bounds checking adds noise that makes the primary algorithm hard to see
- **Suggestion**: extract `nextPathComponent(std::string_view path, size_t& pos)` iterator helper; reduces loop body to a clean `lstat` check per component

---

## High

### `HttpFileServer::handleFileRequest()`
**File**: [lib/src/HttpFileServer.cpp](lib/src/HttpFileServer.cpp#L202) — lines 202–290 (~88 lines)

- Eight or more sequential guard checks (symlink, type, size, access permissions, ETag match, modification time, encoding)
- Mixes file-system validation, HTTP cache-control semantics, MIME detection, and response header assembly
- Each early-return path encodes different HTTP status codes inline
- **Suggestion**: separate into `validateFileSecurity()`, `buildCacheHeaders()`, `assembleFileResponse()`

---

### `HttpFileServer::buildResponse()`
**File**: [lib/src/HttpFileServer.cpp](lib/src/HttpFileServer.cpp#L53) — lines 53–92 (~40 lines)

- Though short in line count, contains seven sequential validation steps each calling a non-trivial helper (`resolveFilePath`, `PathHelper::isPathWithin`, `hasSymlinkComponentWithin`, etc.)
- Six different error-response paths, all sending different HTTP status codes
- High decision density: every line is a meaningful branch
- **Suggestion**: extract a `validateRequest()` → `std::expected<ResolvedPath, HttpError>` helper to consolidate the seven guards

---

### `HttpPollServer::onReadable()`
**File**: [lib/src/HttpPollServer.cpp](lib/src/HttpPollServer.cpp#L122) — lines 122–183 (~61 lines)

- Infinite `for` loop with five distinct break/return conditions
- Interleaves buffer-append operations, incremental request scanning, slowloris timeout check, and request-size limit enforcement in one body
- `WouldBlock` vs. hard-error path distinction is embedded mid-loop rather than at loop boundaries
- **Suggestion**: hoist the timeout and size checks into named predicates; split loop body into `receiveChunk_()` and `checkRequestComplete_()`

---

### `PathHelper::listDirectory()`
**File**: [lib/src/PathHelper.cpp](lib/src/PathHelper.cpp#L168) — lines 168–228 (~60 lines)

- Completely separate Windows (`FindFirstFile`/`FindNextFile`/`HANDLE` cleanup) and Unix (`opendir`/`readdir`/`closedir`) implementations inside one function with an `#ifdef` split
- Both branches separately perform `stat()` on subdirectories in their own loops
- **Suggestion**: extract `listDirectoryWindows_()` and `listDirectoryUnix_()`; or factor out the common post-readdir entry-processing logic

---

### `PollerKqueue::wait()`
**File**: [lib/src/PollerKqueue.cpp](lib/src/PollerKqueue.cpp#L136) — lines 136–229 (~93 lines)

- Non-obvious event-merging optimisation: kqueue can return multiple events for the same fd, so the function accumulates flags into `mergeBits[]` and tracks seen fds in `seenFds[]` before writing results
- Milliseconds → `timespec` conversion with nanosecond arithmetic
- `seenFds` reset pass at the end is easy to misplace when modifying the function
- **Suggestion**: extract `mergeKqueueEvents_()` for the accumulation pass; document the kqueue multi-event contract prominently

---

### `PollerEpoll::wait()`
**File**: [lib/src/PollerEpoll.cpp](lib/src/PollerEpoll.cpp#L107) — lines 107–182 (~75 lines)

- Timeout negotiation between the abstract `Milliseconds` value and `epoll_wait`'s int argument is non-trivial (overflow clamping, `NoTimeout` sentinel)
- EINTR handling with `continue` resets loop state in a non-obvious way
- Bit-manipulation translation from `epoll` event flags to `PollEvent` with conditional OR operations
- **Suggestion**: extract `toEpollTimeout_(Milliseconds)` and `translateEpollEvent_(uint32_t)` helpers

---

### `PollerWSAPoll::wait()`
**File**: [lib/src/PollerWSAPoll.cpp](lib/src/PollerWSAPoll.cpp#L86) — lines 86–148 (~62 lines)

- Timeout deliberately capped at 100 ms to allow signal processing — this non-obvious design decision is not visible at call sites
- Flag translation between `POLLRDNORM`/`POLLWRNORM` and `PollEvent` is duplicated from the timeout-negotiation pass
- **Suggestion**: extract `toWSAPollTimeout_(Milliseconds)` and `translateWSAEvent_(SHORT)` helpers; add a comment at the 100 ms cap explaining why

---

## Moderate

### `HttpPollServer::onWritable()`
**File**: [lib/src/HttpPollServer.cpp](lib/src/HttpPollServer.cpp#L184) — lines 184–225 (~41 lines)

- State reset block updates 7+ fields on `HttpClientState` in sequence — easy to miss a field when adding new state
- Partial-send tracking with `s.sent` offset, keep-alive conditional, and virtual callback calls are interleaved
- **Suggestion**: introduce a `resetClientState_()` helper to own the state fields; makes the keep-alive/disconnect logic more visible

---

### `HttpPollServer::dispatchBuildResponse()`
**File**: [lib/src/HttpPollServer.cpp](lib/src/HttpPollServer.cpp#L103) — lines 103–121 (~18 lines)

- Short but encodes subtle HTTP/1.0 vs HTTP/1.1 keep-alive default difference that is easy to break
- The `closeAfterSend_` determination is spread across a ternary and a header lookup with no explanatory comment
- **Suggestion**: extract to a named `resolveKeepAlive_(const HttpRequest&)` predicate with a comment referencing the relevant RFC section

---

### `HttpFileServer::isAccessAllowed()`
**File**: [lib/src/HttpFileServer.cpp](lib/src/HttpFileServer.cpp#L145) — lines 145–184 (~40 lines)

- Nested loops for path-component extraction combined with `.well-known` exception logic in the same body
- Multiple boundary-condition checks that are individually correct but collectively hard to audit
- **Suggestion**: factor out the component-extraction loop (shared with `hasSymlinkComponentWithin`) into a common iterator

---

### `advanced_file_server.cpp` — `buildResponse()` override
**File**: [examples/advanced_file_server.cpp](examples/advanced_file_server.cpp#L70) — lines 70–108 (~38 lines)

- Mixes three distinct routes: `/test-large`, custom API paths, and fall-through to `HttpFileServer::buildResponse()`
- Inline generation of large synthetic test-file content lives inside the routing branch
- **Suggestion**: extract the test-file generation to a helper and the routing to a small dispatch table; improves readability for readers learning from the example

---

## Summary

| Function | File | Lines | Category |
|---|---|---|---|
| `SocketImpl::connect` | SocketImpl.cpp:253 | ~237 | Critical |
| `ServerBase::run` | ServerBase.h:180 | ~190 | Critical |
| `HttpRequest::parse` | HttpRequest.cpp:44 | ~145 | Critical |
| `HttpResponseParser::tryParseHeaders_` | HttpResponse.cpp:67 | ~111 | Critical |
| `SocketImplHelpers::getLocalAddresses` | SocketImplHelpers.cpp:161 | ~119 | Critical |
| `HttpResponseParser::processChunked_` | HttpResponse.cpp:245 | ~95 | Critical |
| `SocketImplHelpers::resolveToSockaddr` | SocketImplHelpers.cpp:61 | ~70 | Critical |
| `PathHelper::hasSymlinkComponentWithin` | PathHelper.cpp:100 | ~66 | Critical |
| `HttpFileServer::handleFileRequest` | HttpFileServer.cpp:202 | ~88 | High |
| `PollerKqueue::wait` | PollerKqueue.cpp:136 | ~93 | High |
| `PollerEpoll::wait` | PollerEpoll.cpp:107 | ~75 | High |
| `HttpFileServer::buildResponse` | HttpFileServer.cpp:53 | ~40 | High |
| `HttpPollServer::onReadable` | HttpPollServer.cpp:122 | ~61 | High |
| `PathHelper::listDirectory` | PathHelper.cpp:168 | ~60 | High |
| `PollerWSAPoll::wait` | PollerWSAPoll.cpp:86 | ~62 | High |
| `HttpPollServer::onWritable` | HttpPollServer.cpp:184 | ~41 | Moderate |
| `HttpFileServer::isAccessAllowed` | HttpFileServer.cpp:145 | ~40 | Moderate |
| `HttpPollServer::dispatchBuildResponse` | HttpPollServer.cpp:103 | ~18 | Moderate |
| `advanced_file_server::buildResponse` | advanced_file_server.cpp:70 | ~38 | Moderate |
