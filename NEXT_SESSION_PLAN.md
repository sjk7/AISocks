# AISocks — Next Session Feature Plan

**Last updated:** 2026-02-17  
**Repo:** https://github.com/sjk7/AISocks  
**Branch:** `main`  
**Last commit:** `419e610` — "tests: reduce unnecessary sleeps and connect timeout to speed up test suite"  
**Build dir (macOS):** `build-mac/`  
**Build command:** `cd build-mac && cmake --build .`  
**Test command:** `cd build-mac && ctest --output-on-failure`  
**Test suite:** 9 tests, ~0.7s wall time

---

## Completed (this session)

- ✅ Test suite optimised: 2.39 s → ~0.7 s (3×)  
  - `tests/test_construction.cpp`: `TIMEOUT_MS` 500→50 ms, inter-test sleeps 50→10 ms  
  - `tests/test_blocking.cpp`: connector pre-sleep 50→5 ms, hold-open 200→10 ms  
  - `tests/test_loopback_tcp.cpp`: inter-test sleeps 100→10 ms, re-bind wait 50→10 ms, client pre-sleep 50→5 ms, hold-open 100→10 ms  
- ✅ Clean rebuild verified (36 objects), all 9 tests pass  
- ✅ Committed + pushed

---

## Pending Features (implement in this order)

### 1. `setLingerAbort(bool enable)` — RST-on-close

**Files:** `lib/include/Socket.h`, `lib/src/SocketImpl.h`, `lib/src/SocketImpl.cpp`

```cpp
// Socket.h public API
bool setLingerAbort(bool enable);
```

Implementation in `SocketImpl::setLingerAbort`:
```cpp
struct linger lg{};
lg.l_onoff  = enable ? 1 : 0;
lg.l_linger = 0;   // l_linger=0 → RST on close
setsockopt(socketHandle, SOL_SOCKET, SO_LINGER,
           reinterpret_cast<const char*>(&lg), sizeof(lg));
```

**Why:** Sends RST instead of FIN on `close()`. Useful for:
- Test code (avoids TIME_WAIT on rapid connect/disconnect cycles)
- Server accept-loops that want to hard-reject bad clients
- Any scenario where you want immediate port release

**Do NOT implement** blocking-linger (`l_linger > 0`). The proper graceful-shutdown
pattern is `shutdown(Write)` + `receive()`-until-EOF; blocking linger is a footgun.

---

### 2. `sendAll()` — guaranteed full-buffer send

**Files:** `lib/include/Socket.h`, `lib/src/SocketImpl.h`, `lib/src/SocketImpl.cpp`

```cpp
// Socket.h public API
bool sendAll(const void* data, size_t length);
bool sendAll(Span<const std::byte> data);
```

Implementation: loop calling `send()` until all bytes are sent or error occurs.  
Returns `true` if all bytes sent; `false` on error (check `getLastError()`).

Essential for non-blocking sockets and the planned Poller.

---

### 3. `waitReadable()` / `waitWritable()` — single-socket select convenience

**Files:** `lib/include/Socket.h`, `lib/src/SocketImpl.h`, `lib/src/SocketImpl.cpp`

```cpp
// Socket.h public API
bool waitReadable(Milliseconds timeout);
bool waitWritable(Milliseconds timeout);
```

Implementation: wrap `select()` on a single fd (same pattern as the existing
connect timeout loop in `SocketImpl::connect()`).  
Returns `true` if ready within timeout, `false` on timeout, sets error on failure.

Allows callers to do timed receive/send readiness checks without setting `SO_RCVTIMEO`.

---

### 4. `setReusePort(bool)` — `SO_REUSEPORT`

**Files:** `lib/include/Socket.h`, `lib/src/SocketImpl.h`, `lib/src/SocketImpl.cpp`

```cpp
bool setReusePort(bool enable);
```

Implementation:
```cpp
#ifdef SO_REUSEPORT
int optval = enable ? 1 : 0;
setsockopt(socketHandle, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
#else
// Not supported on this platform — set error and return false
#endif
```

Enables multiple processes/threads to bind the same port with kernel load-balancing.
Unavailable on some older Windows versions; guard with `#ifdef SO_REUSEPORT`.

---

### 5. `Socket::connectTo(address, port, timeout)` raw API

**Files:** `lib/include/Socket.h`

Currently timed connect is only accessible via the `ConnectTo{}` constructor.
Manual `bind()` + `connect()` users need a public method.

```cpp
// Socket.h: add alongside existing bind()/listen()/accept() methods
bool connectTo(const std::string& address, Port port,
               Milliseconds timeout = defaultTimeout);
```

Implementation just delegates to the existing `pImpl->connect(address, port, timeout)`.

---

### 6. `Poller` class — native OS readiness notification

**New files:**
- `lib/include/Poller.h`
- `lib/src/PollerKqueue.cpp`  (macOS/BSD — implement first)
- `lib/src/PollerEpoll.cpp`   (Linux)
- `lib/src/PollerWSAPoll.cpp` (Windows)

**CMakeLists.txt** changes needed:
```cmake
# In lib/CMakeLists.txt, inside target_sources:
if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
    target_sources(aiSocksLib PRIVATE src/PollerKqueue.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(aiSocksLib PRIVATE src/PollerEpoll.cpp)
elseif(WIN32)
    target_sources(aiSocksLib PRIVATE src/PollerWSAPoll.cpp)
endif()
```

Also add `tests/test_poller.cpp` to CTest.

#### Public API (`lib/include/Poller.h`)

```cpp
#pragma once
#include "Socket.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace aiSocks {

enum class PollEvent : uint8_t {
    Readable = 1,
    Writable = 2,
    Error    = 4,
};
inline PollEvent operator|(PollEvent a, PollEvent b) {
    return static_cast<PollEvent>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline bool hasFlag(PollEvent set, PollEvent flag) {
    return (static_cast<uint8_t>(set) & static_cast<uint8_t>(flag)) != 0;
}

struct PollResult {
    const Socket* socket;  // borrowed pointer — do not store
    PollEvent events;
};

class Poller {
public:
    Poller();              // throws SocketException on kqueue/epoll create failure
    ~Poller();
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;

    bool add(const Socket& s, PollEvent interest);
    bool modify(const Socket& s, PollEvent interest);
    bool remove(const Socket& s);

    // Returns ready sockets. Empty vector = timeout. Throws on error.
    // timeout = Milliseconds{-1} means wait forever.
    std::vector<PollResult> wait(Milliseconds timeout = Milliseconds{-1});

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace aiSocks
```

#### Backend notes

**`PollerKqueue.cpp` (macOS/BSD — implement first):**
- `kqueue()` in constructor; store fd in `Impl`
- `add`/`modify`: `kevent()` with `EV_ADD|EV_ENABLE`, `EVFILT_READ` and/or `EVFILT_WRITE`
- `remove`: `kevent()` with `EV_DELETE`
- `wait`: `kevent()` with timeout converted to `struct timespec`; retry on `EINTR`
- `EV_EOF` on a read event → set `PollEvent::Readable` (let caller drain to detect close)
- `EV_ERROR` → set `PollEvent::Error`
- Level-triggered behaviour is the default for kqueue (no extra flags needed)

**`PollerEpoll.cpp` (Linux):**
- `epoll_create1(EPOLL_CLOEXEC)` in constructor
- `epoll_ctl(EPOLL_CTL_ADD/MOD/DEL)` for add/modify/remove
- Use level-triggered mode (do NOT set `EPOLLET`)
- `EPOLLIN` → `PollEvent::Readable`, `EPOLLOUT` → `PollEvent::Writable`
- `EPOLLHUP | EPOLLERR | EPOLLRDHUP` → `PollEvent::Error` (also set Readable so caller can drain)
- `epoll_wait()` with timeout in ms; retry on `EINTR`

**`PollerWSAPoll.cpp` (Windows):**
- Maintain a `std::vector<WSAPOLLFD>` and a parallel `std::vector<const Socket*>`
- `WSAPoll()` in `wait()`, convert timeout
- `POLLIN`/`POLLRDNORM` → `PollEvent::Readable`, `POLLOUT`/`POLLWRNORM` → `PollEvent::Writable`
- `POLLERR | POLLHUP | POLLNVAL` → `PollEvent::Error`

**Ownership rule:** `Poller` borrows `Socket` references — sockets must outlive
their registrations. If a socket is destroyed while still registered, call
`remove()` first.

#### Test file sketch (`tests/test_poller.cpp`)

```cpp
// Basic smoke test:
// 1. Create server socket, bind, listen
// 2. Create Poller, add server socket with PollEvent::Readable
// 3. Connect client in a thread
// 4. wait() should return server socket as Readable within 100ms
// 5. accept(), send "hello", receive in client thread — verify
// 6. remove server from poller, confirm it no longer fires
```

---

## Architecture reminders

- `Socket` (public) wraps `std::unique_ptr<SocketImpl>` (pImpl pattern)
- `SocketImpl` is in `lib/src/SocketImpl.h` / `lib/src/SocketImpl.cpp`
- Public header: `lib/include/Socket.h` (~343 lines)
- `namespace aiSocks` throughout
- C++17 standard; `Span<T>` shim already in `Socket.h` for C++20 compatibility
- `defaultTimeout = std::chrono::seconds{30}`; `Milliseconds = std::chrono::milliseconds`
- Platform guards: `_WIN32` / `__linux__` / `__APPLE__` / `(__APPLE__ || __FreeBSD__)`
- `SOCKET_ERROR_CODE` and `INVALID_SOCKET_HANDLE` macros abstract Winsock vs POSIX
- All `setXxx()` methods follow the pattern: check `isValid()`, call `setsockopt()`,
  set error on failure, clear `lastError` on success, return bool
