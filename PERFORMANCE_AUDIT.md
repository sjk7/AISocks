# Hot-path Performance Audit

## Summary

Performance review of the AISocks hot paths, ordered by estimated impact.

---

## Issues

### 1. `PollerWSAPoll`: O(N) `modify()` and `remove()` on Windows — **HIGH** ✅ DONE

**File:** `lib/src/PollerWSAPoll.cpp`

`modify()` and `remove()` both do a linear scan over all registered file descriptors:

```cpp
for (size_t i = 0; i < pImpl_->fds.size(); ++i) {
    if (pImpl_->fds[i].fd == sock) { ... }
}
```

`ServerBase` calls `setClientWritable()` (→ `loop_.modify()`) on **every** successful read/dispatch cycle and again when the write completes. With N clients, each modify is O(N). The epoll and kqueue backends avoid this entirely because the fd is the direct index.

**Fix:** Add a parallel `unordered_map<SOCKET, size_t>` index in `Poller::Impl` on Windows (same pattern the epoll backend already uses with `socketArray`).

---

### 2. `HttpRequest::parse()` called twice per file-serving request — **HIGH** ✅ DONE

**Files:** `lib/src/HttpPollServer.cpp`, `lib/src/HttpFileServer.cpp`

`dispatchBuildResponse` parses the full request to resolve keep-alive:

```cpp
// HttpPollServer::dispatchBuildResponse
const auto req = HttpRequest::parse(s.request);  // parse #1
s.closeAfterSend = resolveKeepAlive_(req);
buildResponse(s);                                 // calls HttpFileServer::buildResponse
```

Then `HttpFileServer::buildResponse` parses again immediately:

```cpp
// HttpFileServer::buildResponse
auto request = HttpRequest::parse(state.request); // parse #2 — redundant
```

Every file request allocates two full `HttpRequest` objects, each with an `unordered_map<string,string>` for headers and another for query params.

**Fix:** Store the parsed result from `dispatchBuildResponse` in `HttpClientState` (or forward it to `buildResponse`) to eliminate the second parse entirely.

---

### 3. `PollerEpoll::wait()`: `events` buffer re-allocated every iteration — **MEDIUM** ✅ DONE

**File:** `lib/src/PollerEpoll.cpp`

```cpp
std::vector<PollResult> Poller::wait(Milliseconds timeout) {
    const int maxEvents = static_cast<int>(pImpl_->socketArray.size()) + 1;
    std::vector<struct epoll_event> events(static_cast<size_t>(maxEvents)); // new allocation every call
```

`pImpl_->resultBuffer` is correctly kept in `Impl` for reuse, but the inbound `events` vector is re-created on every `wait()` call. When `socketArray` is sized to the fd ceiling (65,536 by default), this is a **512 KB heap allocation every event-loop iteration**.

**Fix:** Store the `events` vector in `Impl` alongside `resultBuffer` and only grow it when needed.

---

### 4. `HttpFileServer` non-cached response: triple string allocation — **MEDIUM** ✅ DONE

**File:** `lib/src/HttpFileServer.cpp`

```cpp
StringBuilder response(512 + fileContent.size());
appendOkHeaders(response, ...);                              // fills builder
state.responseBuf = response.toString()                      // alloc #1 (headers copy)
    + std::string(fileContent.begin(), fileContent.end());   // alloc #2 (body copy) + alloc #3 (concat result)
```

`StringBuilder` was already pre-reserved to `512 + fileContent.size()` — enough for both headers and body — yet the body is never appended to it, forcing two extra copies.

**Fix:**

```cpp
StringBuilder response(512 + fileContent.size());
appendOkHeaders(response, ...);
response.append(fileContent.data(), fileContent.size()); // append body into builder
state.responseBuf = response.toString();                 // single allocation
```

Apply the same pattern to the cached path in `sendCachedFile`.

---

### 5. `HttpRequest::header()` constructs a temporary `std::string` for lookup — **LOW** ✅ DONE

**File:** `lib/src/HttpRequest.cpp`

```cpp
auto it = headers.find(std::string(keyData, name.size()));
```

Even though `keyData` already points to a lowercase C-string in a stack buffer, a `std::string` is constructed to satisfy the `unordered_map<string,string>::find()` signature. SSO covers names ≤ ~15 bytes (so `"connection"` is fine), but longer names like `"if-modified-since"` (17 chars) heap-allocate.

**Fix:** Add a transparent hash/equality comparator (`struct CIHash`) on the map to enable heterogeneous lookup and remove the temporary construction entirely.

---

### 6. `FileCache::get()` runs `updateLRU` on every cache hit — **LOW** ✅ DONE

**File:** `lib/src/FileCache.cpp`

```cpp
updateLRU(filePath);   // erase from list + find in index + push_front + reindex — 2 unordered_map ops per hit
return &it->second;
```

Under steady-state traffic (same few popular files served constantly), LRU position rarely changes but the maintenance cost is paid on every request.

**Fix:** Throttle promotion — only move to front if the item is not already in the leading portion of the list (e.g., beyond `lruList_.size() / 2`), without affecting correctness under eviction pressure.

---

## What's Already Well-done

| Area | Location | Notes |
|------|----------|-------|
| Client slot table | `lib/include/ServerBase.h` | Flat `vector` indexed by fd — O(1) insert, erase, and lookup; no hash collisions, no rehash pauses |
| Timeout heap | `lib/include/KeepAliveTimeoutManager.h` | Lazy-deletion min-heap with push-throttle; bounds heap to `O(clients × 4)` even under wrk-level keep-alive churn |
| `sendChunked` | `lib/include/TcpSocket.h` | Tight 64 KB chunk loop, non-blocking, no unnecessary syscalls |
| Request scan position | `lib/include/HttpPollServer.h` | `scanPos` prevents re-scanning already-inspected bytes on multi-chunk arrival |
| Epoll/kqueue backends | `lib/src/PollerEpoll.cpp`, `PollerKqueue.cpp` | O(1) fd→socket lookup via sparse array; `resultBuffer` reused across iterations |
| Request buffer pre-reserve | `lib/include/HttpPollServer.h` | `request.reserve(4096)` avoids reallocation for the vast majority of HTTP requests |
| Zero-copy response dispatch | `lib/include/HttpPollServer.h` | `string_view`-based `responseView` enables zero-copy path for static cached responses |

---

## Suggested Priority Order

| Priority | Location | Effort | Impact |
|----------|----------|--------|--------|
| 1 | `PollerWSAPoll` — add `unordered_map<SOCKET,size_t>` index | Small | High (Windows only) |
| 2 | Eliminate second `HttpRequest::parse()` in `HttpFileServer::buildResponse` | Small | High (every file request) |
| 3 | `PollerEpoll::Impl` — persist `events` buffer, resize only when needed | Trivial | Medium (Linux) |
| 4 | `HttpFileServer::handleFileRequest` — append body to `StringBuilder` before `toString()` | Trivial | Medium |
| 5 | `HttpRequest::header()` — transparent heterogeneous lookup | Small | Low |
| 6 | `FileCache::get()` — throttle LRU promotion | Small | Low |
