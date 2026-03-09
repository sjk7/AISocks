# Unix Domain Socket Implementation Plan

This file is context for a fresh AI session. It contains everything needed to
implement `AF_UNIX` (Unix domain socket) support in the AISocks library.

---

## Goal

Add `AF_UNIX` stream socket support. Approach agreed:
- Store the socket path in the existing `Endpoint::address` string field
- Add `AddressFamily::Unix` as the discriminator (no structural change to `Endpoint`)
- Add `UnixSocket` public type mirroring `TcpSocket` / `UdpSocket`
- Add `SocketFactory::createUnixServer` / `createUnixClient` / `socketpair`
- **macOS + Linux only** (no Windows in this chunk)
- `unlink(path)` on server-side close

---

## Git Workflow (MANDATORY)

**Always follow this sequence:**
1. Make changes
2. `cmake --build build_Mac_arm --config Debug 2>&1 | tail -5`
3. `ctest --test-dir build_Mac_arm --output-on-failure -j1 2>&1`
4. Only if all tests pass (currently 37/37): `git add -A && git commit -m "..." && git push`

Build preset: `build_Mac_arm` (macOS arm64, Ninja, Debug).

---

## Files to Change

### 1. `lib/include/SocketTypes.h`

**Change:** Add `Unix` to the `AddressFamily` enum.

Current:
```cpp
enum class AddressFamily { IPv4, IPv6 };
```

New:
```cpp
enum class AddressFamily { IPv4, IPv6, Unix };
```

No other changes to `Endpoint` — the `address` field holds the socket path,
`port` is `Port{0}` and ignored, `family` is `AddressFamily::Unix`.

`isLoopback()` and `isPrivateNetwork()` are declared here (implemented in
`SocketImpl.cpp`). They need Unix guards — see below.

---

### 2. `lib/src/SocketImpl.h`

**Change:** Add `#include <sys/un.h>` inside the `#else` (non-Windows) block.

Current non-Windows includes:
```cpp
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>      // IFF_LOOPBACK
#include <netinet/tcp.h>
```

Add after `<net/if.h>`:
```cpp
#include <sys/un.h>      // sockaddr_un for AF_UNIX
```

---

### 3. `lib/src/SocketImpl.cpp` — constructor

**Location:** `SocketImpl::SocketImpl(SocketType type, AddressFamily family)`,
around line 76.

Current:
```cpp
    int af = (family == AddressFamily::IPv6) ? AF_INET6 : AF_INET;
    int sockType = (type == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
    int protocol = (type == SocketType::TCP) ? IPPROTO_TCP : IPPROTO_UDP;
```

New:
```cpp
#ifndef _WIN32
    int af = (family == AddressFamily::Unix)  ? AF_UNIX
           : (family == AddressFamily::IPv6)  ? AF_INET6
                                              : AF_INET;
#else
    int af = (family == AddressFamily::IPv6) ? AF_INET6 : AF_INET;
#endif
    int sockType = (type == SocketType::TCP) ? SOCK_STREAM : SOCK_DGRAM;
    int protocol = (family == AddressFamily::Unix) ? 0
               : (type == SocketType::TCP)          ? IPPROTO_TCP
                                                    : IPPROTO_UDP;
```

---

### 4. `lib/src/SocketImpl.cpp` — `accept()`

**Location:** around line 225, where `clientFamily` is deduced from `sa_family`.

Current:
```cpp
            AddressFamily clientFamily
                = (reinterpret_cast<sockaddr*>(&clientAddr)->sa_family
                      == AF_INET6)
                ? AddressFamily::IPv6
                : AddressFamily::IPv4;
```

New:
```cpp
            int sa_fam = reinterpret_cast<sockaddr*>(&clientAddr)->sa_family;
            AddressFamily clientFamily =
#ifndef _WIN32
                (sa_fam == AF_UNIX)  ? AddressFamily::Unix  :
#endif
                (sa_fam == AF_INET6) ? AddressFamily::IPv6  :
                                       AddressFamily::IPv4;
```

---

### 5. `lib/src/SocketImpl.cpp` — `endpointFromSockaddr()`

**Location:** around line 1186 — function that converts `sockaddr_storage` → `Endpoint`.

Current function body (relevant part):
```cpp
Endpoint SocketImpl::endpointFromSockaddr(const sockaddr_storage& addr) {
    Endpoint ep;
    if (addr.ss_family == AF_INET6) {
        // ... IPv6 ...
    } else {
        // ... IPv4 ...
    }
    return ep;
}
```

New — add `AF_UNIX` arm **before** the existing if/else:
```cpp
Endpoint SocketImpl::endpointFromSockaddr(const sockaddr_storage& addr) {
    Endpoint ep;
#ifndef _WIN32
    if (addr.ss_family == AF_UNIX) {
        ep.family = AddressFamily::Unix;
        ep.port   = Port{0};
        const auto* un = reinterpret_cast<const sockaddr_un*>(&addr);
        ep.address = un->sun_path; // may be empty for anonymous sockets
        return ep;
    }
#endif
    if (addr.ss_family == AF_INET6) {
        // ... existing IPv6 code unchanged ...
    } else {
        // ... existing IPv4 code unchanged ...
    }
    return ep;
}
```

---

### 6. `lib/src/SocketImpl.cpp` — `isLoopback()` / `isPrivateNetwork()`

Search for the implementations of `Endpoint::isLoopback()` and
`Endpoint::isPrivateNetwork()`. Add a Unix guard at the top of each:

```cpp
bool Endpoint::isLoopback() const {
#ifndef _WIN32
    if (family == AddressFamily::Unix) return true; // always local
#endif
    // ... existing IP logic unchanged ...
}

bool Endpoint::isPrivateNetwork() const {
#ifndef _WIN32
    if (family == AddressFamily::Unix) return true; // always local
#endif
    // ... existing IP logic unchanged ...
}
```

---

### 7. `lib/src/SocketImplHelpers.cpp` — `resolveToSockaddr()`

**Location:** line 61. This is the core change.

Current signature:
```cpp
SocketError resolveToSockaddr(const std::string& address, Port port,
    AddressFamily family, SocketType sockType, bool doDns,
    sockaddr_storage& out, socklen_t& outLen, int* gaiErr) {
    if (family == AddressFamily::IPv6) {
        // ... IPv6 ...
    } else {
        // ... IPv4 ...
    }
    return SocketError::None;
}
```

Add `AF_UNIX` early-exit **before** the existing IPv6 branch:
```cpp
SocketError resolveToSockaddr(const std::string& address, Port port,
    AddressFamily family, SocketType sockType, bool doDns,
    sockaddr_storage& out, socklen_t& outLen, int* gaiErr) {
#ifndef _WIN32
    if (family == AddressFamily::Unix) {
        if (address.empty()) return SocketError::BindFailed;
        if (address.size() >= sizeof(sockaddr_un::sun_path))
            return SocketError::BindFailed; // path too long (max ~107 chars)
        sockaddr_un un{};
        un.sun_family = AF_UNIX;
        std::memcpy(un.sun_path, address.c_str(), address.size() + 1);
        std::memset(&out, 0, sizeof(out));
        std::memcpy(&out, &un, sizeof(un));
        outLen = static_cast<socklen_t>(
            offsetof(sockaddr_un, sun_path) + address.size() + 1);
        return SocketError::None;
    }
#endif
    if (family == AddressFamily::IPv6) {
        // ... existing IPv6 code unchanged ...
```

---

### 8. New file: `lib/include/UnixSocket.h`

Model this exactly on `TcpSocket.h`. Key differences:
- Constructor takes a path string instead of IP+port
- No `setNoDelay`, no `sendfile` (TCP-only features)
- `~UnixSocket()` does **not** unlink — only the server-side factory does that
  (the socket returned by `accept()` shares the path but must not unlink it)
- `friend class SocketFactory`

```cpp
#ifndef AISOCKS_UNIX_SOCKET_H
#define AISOCKS_UNIX_SOCKET_H

#ifndef _WIN32

#include "Socket.h"

namespace aiSocks {

// ---------------------------------------------------------------------------
// UnixSocket — stream socket over AF_UNIX (Linux/macOS).
//
// The socket path is stored in Endpoint::address; port is always Port{0}.
// Mirrors TcpSocket API where applicable.
// ---------------------------------------------------------------------------
class UnixSocket : public Socket {
    friend class SocketFactory;

public:
    // Server socket: socket() → SO_REUSEADDR → bind() → listen().
    explicit UnixSocket(const std::string& path);

    // Client socket: socket() → connect().
    // (Use SocketFactory::createUnixClient for error-checked creation.)
    struct ConnectTag {};
    UnixSocket(ConnectTag, const std::string& path);

    ~UnixSocket() = default;
    UnixSocket(UnixSocket&&) noexcept = default;
    UnixSocket& operator=(UnixSocket&&) noexcept = default;

    [[nodiscard]] bool listen(int backlog = 128) { return doListen(backlog); }
    [[nodiscard]] std::unique_ptr<UnixSocket> accept();

    int send(const void* data, size_t length)  { return doSend(data, length); }
    int receive(void* buffer, size_t length)   { return doReceive(buffer, length); }
    bool sendAll(const void* data, size_t length)   { return doSendAll(data, length); }
    bool receiveAll(void* buffer, size_t length)    { return doReceiveAll(buffer, length); }

private:
    explicit UnixSocket(std::unique_ptr<SocketImpl> impl);
};

} // namespace aiSocks

#endif // !_WIN32
#endif // AISOCKS_UNIX_SOCKET_H
```

---

### 9. New file: `lib/src/UnixSocket.cpp`

```cpp
#ifndef _WIN32
#include "UnixSocket.h"
#include "SocketImpl.h"
#include <sys/un.h>

namespace aiSocks {

// Server constructor: just create the socket; caller does bind+listen via factory.
UnixSocket::UnixSocket(const std::string& /*path*/)
    : Socket(SocketType::TCP, AddressFamily::Unix) {}

// Client constructor
UnixSocket::UnixSocket(ConnectTag, const std::string& path)
    : Socket(SocketType::TCP, AddressFamily::Unix) {
    doConnect(path, Port{0}, Milliseconds{0}); // blocking connect, no timeout
}

// Accept constructor (wraps an accepted fd)
UnixSocket::UnixSocket(std::unique_ptr<SocketImpl> impl)
    : Socket(std::move(impl)) {}

std::unique_ptr<UnixSocket> UnixSocket::accept() {
    auto childImpl = impl_->accept();
    if (!childImpl) return nullptr;
    return std::unique_ptr<UnixSocket>(new UnixSocket(std::move(childImpl)));
}

} // namespace aiSocks
#endif
```

---

### 10. `lib/include/SocketFactory.h` — new factory methods

Add to the `SocketFactory` class (after the UDP section):

```cpp
// -----------------------------------------------------------------------
// Unix domain socket creation (Linux/macOS only)
// -----------------------------------------------------------------------
#ifndef _WIN32
    // Create a Unix stream server (bound and listening).
    // The returned socket owns the path: when it is destroyed, unlink() is
    // called automatically via a custom deleter stored alongside the socket.
    // For simplicity in this first implementation, callers are responsible
    // for calling unlink() if they need to remove the path before the
    // socket is destroyed.
    static Result<UnixSocket> createUnixServer(const std::string& path);

    // Create a connected Unix stream client.
    static Result<UnixSocket> createUnixClient(const std::string& path);

    // Create a connected anonymous pair (no filesystem path needed).
    // Returns [server-side, client-side] as a pair.
    static std::pair<Result<UnixSocket>, Result<UnixSocket>> createUnixPair();
#endif
```

Also add `#include "UnixSocket.h"` to the includes at the top of `SocketFactory.h`.

---

### 11. `lib/src/SocketFactory.cpp` — implement new methods

```cpp
#ifndef _WIN32
Result<UnixSocket> SocketFactory::createUnixServer(const std::string& path) {
    auto impl = std::make_unique<SocketImpl>(SocketType::TCP, AddressFamily::Unix);
    if (!impl->isValid())
        return Result<UnixSocket>::failure(impl->getLastError(), impl->getLastErrorMessage());

    if (!impl->bind(path, Port{0}))
        return Result<UnixSocket>::failure(impl->getLastError(), impl->getLastErrorMessage());

    if (!impl->listen(128))
        return Result<UnixSocket>::failure(impl->getLastError(), impl->getLastErrorMessage());

    return Result<UnixSocket>::success(UnixSocket(std::move(impl)));
}

Result<UnixSocket> SocketFactory::createUnixClient(const std::string& path) {
    auto impl = std::make_unique<SocketImpl>(SocketType::TCP, AddressFamily::Unix);
    if (!impl->isValid())
        return Result<UnixSocket>::failure(impl->getLastError(), impl->getLastErrorMessage());

    if (!impl->connect(path, Port{0}, defaultTimeout))
        return Result<UnixSocket>::failure(impl->getLastError(), impl->getLastErrorMessage());

    return Result<UnixSocket>::success(UnixSocket(std::move(impl)));
}

std::pair<Result<UnixSocket>, Result<UnixSocket>> SocketFactory::createUnixPair() {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        auto err = Result<UnixSocket>::failure(SocketError::CreateFailed, "socketpair failed");
        return {err, err};
    }
    auto implA = std::make_unique<SocketImpl>(fds[0], SocketType::TCP, AddressFamily::Unix);
    auto implB = std::make_unique<SocketImpl>(fds[1], SocketType::TCP, AddressFamily::Unix);
    return {
        Result<UnixSocket>::success(UnixSocket(std::move(implA))),
        Result<UnixSocket>::success(UnixSocket(std::move(implB)))
    };
}
#endif
```

---

### 12. `tests/CMakeLists.txt`

Add `test_unix_socket` to the `TEST_SOURCES` list (before the closing `)`).

---

### 13. New file: `tests/test_unix_socket.cpp`

```cpp
// Tests for UnixSocket and SocketFactory Unix methods.
// Linux/macOS only — Windows builds skip this file via #ifndef _WIN32 guard.

#ifndef _WIN32
#include "UnixSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>
#include <cstdio>
#include <sys/stat.h>

using namespace aiSocks;
using namespace std::chrono_literals;

static const std::string kPath = "/tmp/aisocks_test_unix.sock";

int main() {
    printf("=== UnixSocket Tests ===\n");

    // Clean up any leftover socket from a previous failed run
    ::unlink(kPath.c_str());

    BEGIN_TEST("createUnixServer succeeds");
    {
        ::unlink(kPath.c_str());
        auto r = SocketFactory::createUnixServer(kPath);
        REQUIRE(r.isSuccess());
        REQUIRE(r.value().isValid());
        ::unlink(kPath.c_str());
    }

    BEGIN_TEST("createUnixClient connects to server");
    {
        ::unlink(kPath.c_str());
        auto srvResult = SocketFactory::createUnixServer(kPath);
        REQUIRE(srvResult.isSuccess());
        auto& srv = srvResult.value();

        std::thread serverThread([&srv]() {
            auto client = srv.accept();
            if (client) {
                const char msg[] = "hello";
                client->sendAll(msg, sizeof(msg) - 1);
            }
        });

        auto cliResult = SocketFactory::createUnixClient(kPath);
        REQUIRE(cliResult.isSuccess());
        auto& cli = cliResult.value();

        char buf[16] = {};
        bool ok = cli.receiveAll(buf, 5);
        REQUIRE(ok);
        REQUIRE(std::string(buf, 5) == "hello");

        serverThread.join();
        ::unlink(kPath.c_str());
    }

    BEGIN_TEST("createUnixPair: bidirectional communication");
    {
        auto [ra, rb] = SocketFactory::createUnixPair();
        REQUIRE(ra.isSuccess());
        REQUIRE(rb.isSuccess());

        auto& a = ra.value();
        auto& b = rb.value();

        const char ping[] = "ping";
        a.sendAll(ping, sizeof(ping) - 1);

        char buf[8] = {};
        b.receiveAll(buf, 4);
        REQUIRE(std::string(buf, 4) == "ping");

        const char pong[] = "pong";
        b.sendAll(pong, sizeof(pong) - 1);
        char buf2[8] = {};
        a.receiveAll(buf2, 4);
        REQUIRE(std::string(buf2, 4) == "pong");
    }

    return test_summary();
}
#else
int main() { return 0; }
#endif
```

---

## Known Subtleties

### `SocketImpl::connect()` with Unix path
`connect()` calls `resolveToSockaddr()` internally. The new `AF_UNIX` arm in
`resolveToSockaddr()` fills `sockaddr_un` from `address` (the path). The
`port` argument is `Port{0}` and is ignored in the Unix arm. This works
because `connect()` passes `addressFamily` which will be `AddressFamily::Unix`.

### Blocking mode
`SO_NOSIGPIPE` is set in `SocketImpl`'s constructor for TCP sockets
(`#ifdef SO_NOSIGPIPE`). That guard still applies — Unix sockets also benefit
from it on macOS, so no change needed there.

### `setNoDelay`
`TcpSocket::setNoDelay()` calls `setsockopt(IPPROTO_TCP, TCP_NODELAY)`. Unix
sockets ignore TCP options — `setsockopt` will return an error but it's
silently swallowed. `UnixSocket` simply doesn't expose `setNoDelay()`.

### `Endpoint::toString()` for Unix
Returns `"/tmp/foo.sock:0"` — ugly but harmless. Acceptable for now.

### Path length limit
`sockaddr_un::sun_path` is 104 bytes on macOS, 108 bytes on Linux. The
`resolveToSockaddr()` guard rejects paths `>= sizeof(sun_path)`.

### Abstract namespace (Linux only)
Not implemented in this chunk. Abstract namespace paths start with `\0`. Leave
for later.

### Windows
`AF_UNIX` is available on Windows 10 1803+ via `#include <afunix.h>`.
Out of scope for this chunk — all Unix-socket code is inside `#ifndef _WIN32`.

### `unlink()` ownership
The server socket file should be removed when the server is done. For now,
callers are responsible for calling `::unlink(path.c_str())` after the server
socket is destroyed. A future enhancement could store the path in `UnixSocket`
and call `unlink` in a destructor — but that requires knowing whether a given
`UnixSocket` is the original server (not an accepted child). The factory could
handle this with a custom deleter or a `bool ownsPath` member.

---

## Result<T> API (for reference when writing SocketFactory methods)

```cpp
// Success:
Result<T>::success(T value)
// Failure:
Result<T>::failure(SocketError, const std::string& message)
// OR, using the error context form used elsewhere:
Result<T>::failure(SocketError err, "description string", sysErrno, isDns)
```

Check `lib/src/SocketFactory.cpp` for existing examples of the exact
`Result<T>::failure(...)` overload in use — there are two overloads and the
signatures differ slightly between them.

---

## Checklist for the new session

- [ ] `AddressFamily::Unix` added to `SocketTypes.h`
- [ ] `#include <sys/un.h>` added to `SocketImpl.h` (non-Windows block)
- [ ] `SocketImpl` constructor: `AF_UNIX` arm
- [ ] `SocketImpl::accept()`: `AF_UNIX` arm for clientFamily deduction
- [ ] `endpointFromSockaddr()`: `AF_UNIX` arm
- [ ] `Endpoint::isLoopback()`, `isPrivateNetwork()`: Unix guards
- [ ] `resolveToSockaddr()`: `AF_UNIX` early-exit
- [ ] `UnixSocket.h` created
- [ ] `UnixSocket.cpp` created
- [ ] `SocketFactory.h`: new declarations + `#include "UnixSocket.h"`
- [ ] `SocketFactory.cpp`: new implementations
- [ ] `UnixSocket.cpp` added to `lib/CMakeLists.txt` (check how other .cpp files are listed there)
- [ ] `test_unix_socket.cpp` created
- [ ] `test_unix_socket` added to `tests/CMakeLists.txt` TEST_SOURCES
- [ ] Build clean
- [ ] All 37 (+ new) tests pass
- [ ] Commit and push
