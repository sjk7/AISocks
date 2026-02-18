# AISocks — Next Session Plan

**Last updated:** 2026-02-18  
**Repo:** https://github.com/sjk7/AISocks  
**Branch:** `main`  
**Last commit:** `00eaf93` — "style: add workspace .clang-format and normalise all source files"  
**Build dir (macOS):** `build-mac/`  
**Build command:** `ninja -C build-mac`  
**Test command:** `cd build-mac && ctest --output-on-failure`  
**Test suite:** 9 tests, ~0.7 s wall time  
**Standard:** C++17, clang++ `-Wall -Wextra -Wpedantic -Werror -arch arm64`

---

## What is complete

All of these are committed and pushed.

- All PVS-Studio warnings fixed (V641, V1071, V512, V1086, V1007, V807, V830,
  V565, V103, V106)
- `receiveAll()` added (exact-read counterpart to `sendAll()`), with two tests
- `noexcept` on all non-throwing accessors (`isValid`, `getLastError`,
  `getAddressFamily`, `isBlocking`, `close`) and `Poller` operators
- `setLingerAbort(bool)`, `setReusePort(bool)`, `waitReadable()`,
  `waitWritable()`, `setSendBufferSize()`, `setReceiveBufferSize()`,
  `setKeepAlive()`, `setSendTimeout()` — all implemented
- `Poller` class: kqueue (macOS), epoll (Linux), WSAPoll (Windows), plus
  async-connect support and `test_poller.cpp`
- `.clang-format` workspace config; all source files normalised
- Test suite wall time: 2.4 s → 0.7 s (3×) via reduced sleeps/timeouts

---

## The one remaining task: TCP/UDP type-safe split

### Goal

Replace the single `Socket` class with `TcpSocket` and `UdpSocket`, each
exposing only the methods that make sense for their protocol. Zero logic
duplication — no `SocketCommon` layer.

### Architecture (fully agreed, not yet implemented)

The pImpl firewall is **already** in `Socket`. `TcpSocket` and `UdpSocket`
inherit `Socket` and stand **behind** that same firewall for free. They never
include `SocketImpl.h` (except `TcpSocket.cpp` for `accept()`). There is one
firewall, shared by inheritance.

```
TcpSocket.h      includes Socket.h only (forward-decl of SocketImpl via Socket)
                     calls do*() protected bridge methods
                              |
                    Socket.h  (forward-declares SocketImpl, owns unique_ptr<SocketImpl>)
                    Socket.cpp (includes SocketImpl.h — this IS the firewall)
                              |
                    SocketImpl.h / SocketImpl.cpp
```

`TcpSocket.cpp` is the only file besides `Socket.cpp` that sees `SocketImpl.h`,
and only because `accept()` must move a `unique_ptr<SocketImpl>` into a new
`TcpSocket`. This does not create a second firewall — it uses the existing one.

### Changes required

#### Files to DELETE first

| File | Reason |
|---|---|
| `lib/include/SocketBase.h` | Abandoned intermediate CRTP design |
| `lib/src/SocketCommon.cpp` | Goes with SocketBase.h |

The stale `lib/include/TcpSocket.h` and `lib/include/UdpSocket.h` also exist
from the abandoned design and must be **replaced** (not just deleted).

#### `lib/include/Socket.h` — modify in place

1. Remove `virtual` from `~Socket()`.
2. Move `~Socket()` to `protected`.
3. Move all protocol-specific public methods to `protected`, renamed with `do`
   prefix:

   | Old public name | New protected name | Return type change? |
   |---|---|---|
   | `bind` | `doBind` | no |
   | `listen` | `doListen` | no |
   | `accept` | `doAccept` | yes — returns `unique_ptr<SocketImpl>` (not `unique_ptr<Socket>`) |
   | `connect` | `doConnect` | no |
   | `send(void*, size_t)` | `doSend` | no |
   | `receive(void*, size_t)` | `doReceive` | no |
   | `sendAll(void*, size_t)` | `doSendAll` | no |
   | `sendAll(Span)` | `doSendAll` | no |
   | `receiveAll(void*, size_t)` | `doReceiveAll` | no |
   | `receiveAll(Span)` | `doReceiveAll` | no |
   | `sendTo` | `doSendTo` | no |
   | `receiveFrom` | `doReceiveFrom` | no |
   | `send(Span)` | `doSend` | no |
   | `receive(Span)` | `doReceive` | no |

4. Keep ALL option/query methods public and unchanged:  
   `setBlocking`, `isBlocking`, `waitReadable`, `waitWritable`,
   `setReuseAddress`, `setReusePort`, `setTimeout`, `setSendTimeout`,
   `setNoDelay`, `setReceiveBufferSize`, `setSendBufferSize`, `setKeepAlive`,
   `setLingerAbort`, `shutdown`, `close`, `isValid`, `getAddressFamily`,
   `getLastError`, `getErrorMessage`, `getLocalEndpoint`, `getPeerEndpoint`,
   `getNativeHandle`, and all static utilities.

5. Make constructors `protected` (only derived classes should instantiate):
   ```cpp
   protected:
       Socket(SocketType, AddressFamily);
       Socket(SocketType, AddressFamily, const ServerBind&);
       Socket(SocketType, AddressFamily, const ConnectTo&);
       explicit Socket(std::unique_ptr<SocketImpl>);
       ~Socket();  // non-virtual, protected
       Socket(Socket&&) noexcept;
       Socket& operator=(Socket&&) noexcept;
   ```

6. `doAccept()` declaration in the header should return
   `std::unique_ptr<SocketImpl>` — requires a forward declaration of
   `SocketImpl` at the top of `Socket.h`. `SocketImpl` is already forward-
   declared there for the `pImpl` member, so just adjust the return type.

#### `lib/src/Socket.cpp` — minor rename only

Every method body is already correct — just rename:
- `Socket::bind` → `Socket::doBind`
- `Socket::listen` → `Socket::doListen`
- `Socket::accept` → `Socket::doAccept`, change return type:
  ```cpp
  std::unique_ptr<SocketImpl> Socket::doAccept() {
      assert(pImpl);
      return pImpl->accept();   // returns unique_ptr<SocketImpl> directly
  }
  ```
- `Socket::connect` → `Socket::doConnect`
- `Socket::send` → `Socket::doSend` (both overloads)
- `Socket::receive` → `Socket::doReceive` (both overloads)
- `Socket::sendAll` → `Socket::doSendAll` (both overloads)
- `Socket::receiveAll` → `Socket::doReceiveAll` (both overloads)
- `Socket::sendTo` → `Socket::doSendTo` (both overloads)
- `Socket::receiveFrom` → `Socket::doReceiveFrom` (both overloads)

Everything else (constructors, dtor, option methods) is unchanged.

#### `lib/include/TcpSocket.h` — replace entirely

```cpp
#pragma once
#include "Socket.h"
#include <memory>

namespace aiSocks {

namespace detail {
    // C++17-compatible no-op progress callback default type.
    struct NoOpProgress {
        void operator()(size_t /*sent*/, size_t /*total*/) const noexcept {}
    };
} // namespace detail

class TcpSocket : public Socket {
public:
    // Constructors — defined in TcpSocket.cpp
    explicit TcpSocket(AddressFamily family = AddressFamily::IPv4);
    TcpSocket(AddressFamily family, const ServerBind& cfg);
    TcpSocket(AddressFamily family, const ConnectTo& cfg);
    ~TcpSocket() = default;   // chains to Socket::~Socket() out-of-line in Socket.cpp

    TcpSocket(TcpSocket&&) noexcept = default;
    TcpSocket& operator=(TcpSocket&&) noexcept = default;

    // Protocol methods — all inline one-liners
    bool bind(const std::string& address, Port port) {
        return doBind(address, port);
    }
    bool listen(int backlog = 128) { return doListen(backlog); }
    bool connect(const std::string& address, Port port,
                 Milliseconds timeout = defaultTimeout) {
        return doConnect(address, port, timeout);
    }
    int send(const void* data, size_t length) { return doSend(data, length); }
    int send(Span<const std::byte> data)      { return doSend(data); }
    int receive(void* buffer, size_t length)  { return doReceive(buffer, length); }
    int receive(Span<std::byte> buffer)       { return doReceive(buffer); }

    bool sendAll(const void* data, size_t length) {
        return doSendAll(data, length);
    }
    bool sendAll(Span<const std::byte> data) { return doSendAll(data); }

    bool receiveAll(void* buffer, size_t length) {
        return doReceiveAll(buffer, length);
    }
    bool receiveAll(Span<std::byte> buffer) { return doReceiveAll(buffer); }

    // Template sendAll/receiveAll with progress callback (C++17).
    // Fn signature: void(size_t bytesTransferred, size_t total)
    template <typename Fn = detail::NoOpProgress>
    bool sendAll(const void* data, size_t length, Fn&& progress = Fn{}) {
        // Calls doSendAll in a loop via pImpl — but we can't access pImpl directly.
        // Strategy: call doSend in a loop here, invoking progress each iteration.
        // (Alternatively, just call doSendAll and invoke progress once at end —
        // decide which granularity is useful before implementing.)
        (void)progress;
        return doSendAll(data, length);
    }

    // accept() — defined in TcpSocket.cpp (needs SocketImpl.h)
    std::unique_ptr<TcpSocket> accept();

private:
    // Private constructor used by accept() to wrap an accepted SocketImpl.
    // Defined in TcpSocket.cpp.
    explicit TcpSocket(std::unique_ptr<SocketImpl> impl);
};

} // namespace aiSocks
```

> **Note on the template sendAll/receiveAll with progress callback:** The
> simple approach calls `doSendAll` and then notifies at the end. If per-chunk
> notification is desired, the loop must be in `TcpSocket` calling `doSend`
> repeatedly. Decide before implementing.

#### `lib/src/TcpSocket.cpp` — create new (~25 lines)

```cpp
#include "TcpSocket.h"
#include "SocketImpl.h"   // Only .cpp that needs this besides Socket.cpp

namespace aiSocks {

TcpSocket::TcpSocket(AddressFamily family)
    : Socket(SocketType::TCP, family) {}

TcpSocket::TcpSocket(AddressFamily family, const ServerBind& cfg)
    : Socket(SocketType::TCP, family, cfg) {}

TcpSocket::TcpSocket(AddressFamily family, const ConnectTo& cfg)
    : Socket(SocketType::TCP, family, cfg) {}

TcpSocket::TcpSocket(std::unique_ptr<SocketImpl> impl)
    : Socket(std::move(impl)) {}

std::unique_ptr<TcpSocket> TcpSocket::accept() {
    auto clientImpl = doAccept();
    if (!clientImpl) return nullptr;
    return std::unique_ptr<TcpSocket>(new TcpSocket(std::move(clientImpl)));
}

} // namespace aiSocks
```

#### `lib/include/UdpSocket.h` — replace entirely

```cpp
#pragma once
#include "Socket.h"

namespace aiSocks {

class UdpSocket : public Socket {
public:
    explicit UdpSocket(AddressFamily family = AddressFamily::IPv4);
    ~UdpSocket() = default;

    UdpSocket(UdpSocket&&) noexcept = default;
    UdpSocket& operator=(UdpSocket&&) noexcept = default;

    bool bind(const std::string& address, Port port) {
        return doBind(address, port);
    }
    bool connect(const std::string& address, Port port,
                 Milliseconds timeout = defaultTimeout) {
        return doConnect(address, port, timeout);
    }
    int send(const void* data, size_t length) { return doSend(data, length); }
    int send(Span<const std::byte> data)      { return doSend(data); }
    int receive(void* buffer, size_t length)  { return doReceive(buffer, length); }
    int receive(Span<std::byte> buffer)       { return doReceive(buffer); }
    int sendTo(const void* data, size_t length, const Endpoint& remote) {
        return doSendTo(data, length, remote);
    }
    int sendTo(Span<const std::byte> data, const Endpoint& remote) {
        return doSendTo(data, remote);
    }
    int receiveFrom(void* buffer, size_t length, Endpoint& remote) {
        return doReceiveFrom(buffer, length, remote);
    }
    int receiveFrom(Span<std::byte> buffer, Endpoint& remote) {
        return doReceiveFrom(buffer, remote);
    }
    bool setBroadcast(bool enable) { return setSocketOpt(enable); }
    // No listen(), accept(), sendAll(), receiveAll() — not valid for UDP
};

} // namespace aiSocks
```

> **setBroadcast:** Check how `setBroadcast` / `SO_BROADCAST` is handled in
> `SocketImpl`. If it's not there yet, add it.

#### `lib/src/UdpSocket.cpp` — create new (~10 lines)

```cpp
#include "UdpSocket.h"

namespace aiSocks {

UdpSocket::UdpSocket(AddressFamily family)
    : Socket(SocketType::UDP, family) {}

} // namespace aiSocks
```

#### `lib/CMakeLists.txt` — add new sources

Add `TcpSocket.cpp` and `UdpSocket.cpp` to the library target. Do NOT add
`SocketCommon.cpp` (deleted).

#### Tests — update to use typed sockets

All test files that currently create `Socket` directly with TCP should switch to
`TcpSocket`. UDP tests should use `UdpSocket`. Tests that exercise base class
Option methods (setTimeout, setNoDelay, etc.) can still use either concrete
type.

Files to update:
- `tests/test_construction.cpp`
- `tests/test_blocking.cpp`
- `tests/test_loopback_tcp.cpp`
- `tests/test_move_semantics.cpp`
- `tests/test_ip_utils.cpp` (if it constructs sockets)
- `tests/test_poller.cpp`
- `tests/test_socket_basics.cpp`
- `tests/test_error_handling.cpp`
- `tests/test_error_messages.cpp`
- `examples/*.cpp`

---

## Implementation order

1. `rm lib/include/SocketBase.h lib/src/SocketCommon.cpp`
2. Modify `lib/include/Socket.h` — protected ctor/dtor, `do*()` bridges
3. Modify `lib/src/Socket.cpp` — rename methods to `do*()`, fix `doAccept` return
4. Replace `lib/include/TcpSocket.h`
5. Replace `lib/include/UdpSocket.h`
6. Create `lib/src/TcpSocket.cpp`
7. Create `lib/src/UdpSocket.cpp`
8. Update `lib/CMakeLists.txt`
9. Build — fix any compile errors
10. Update all test files and examples
11. Build + `ctest --output-on-failure` — all 9 tests green
12. Commit

---

## Key C++ notes

### Protected non-virtual destructor

`Socket::~Socket()` is `protected` and **not** `virtual`. This means:

- You cannot call `delete basePtr` where `basePtr` is `Socket*` — compile error.
- No vtable, no overhead.
- `delete tcpPtr` where `tcpPtr` is `TcpSocket*` works fine (calls
  `~TcpSocket()` → `~Socket()` in the normal non-virtual chain).
- Only sensible because `Socket` is not meant to be used polymorphically through
  a base pointer. If you ever want `unique_ptr<Socket>` storing a `TcpSocket`,
  the protected destructor will block it — intentional.

### C++17 default template parameter for progress callback

`Fn = decltype([](size_t, size_t) noexcept {})` is C++20 (stateless lambda as
default template argument). For C++17 use the `detail::NoOpProgress` struct
shown above, or provide two overloads (one with `Fn&&`, one without).

### Single pImpl firewall

`TcpSocket` and `UdpSocket` never include `SocketImpl.h` except in
`TcpSocket.cpp` (for `accept()`). They call `do*()` methods whose bodies live
in `Socket.cpp` — the one place `SocketImpl.h` is included. One firewall,
shared for free by inheritance.

```
TcpSocket.h     → no SocketImpl
TcpSocket.cpp   → SocketImpl.h (accept only)
UdpSocket.h     → no SocketImpl
UdpSocket.cpp   → no SocketImpl
Socket.h        → forward declare SocketImpl (for unique_ptr member)
Socket.cpp      → #include "SocketImpl.h"  ← THE FIREWALL
```

---

## Repository housekeeping notes

- `.clang-format` is in workspace root; all future saves will reformat
  automatically if the editor respects it.
- `build-mac/` is the active macOS build dir. `build/` is a Windows MSVC
  artefact dir — ignore it on macOS.
- `compile_commands.json` is symlinked at root from `build-mac/` for
  clangd/IntelliSense.
