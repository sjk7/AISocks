# aiSocks

Cross-platform C++17 socket library — and a high-performance poll-driven HTTP/1.x server built on top of it.

Zero dependencies beyond a standard C++17 compiler and CMake.

## Performance

Benchmarked on macOS (Apple Silicon), Release build, `wrk -t12 -c5000 -d30s`:

| Metric | aiSocks | nginx | Δ |
|---|---|---|---|
| Requests/sec | **77,243** | 21,385 | ✅ +261% |
| Transfer/sec | **35.95 MB** | 9.95 MB | ✅ +261% |
| Total requests (30 s) | **2,325,324** | 643,274 | ✅ +261% |
| Avg latency | **25.39 ms** | 41.40 ms | ✅ −39% |
| Read errors | **0** | 154,282 | ✅ |

## Features

- ✅ Cross-platform (Windows, macOS, Linux)
- ✅ Clean pimpl API — platform headers never leak into user code
- ✅ IPv4 and IPv6
- ✅ TCP and UDP
- ✅ Blocking and non-blocking modes
- ✅ Poll-driven `ServerBase<T>` — single-thread, zero allocations in the hot path
- ✅ `HttpPollServer` — HTTP/1.x framing, keep-alive, streaming, zero-copy static responses
- ✅ kqueue backend (macOS/BSD) with lazy-deletion timeout heap
- ✅ AddressSanitizer / MemorySanitizer / UBSan build presets
- ✅ Modern C++17, CMake, Ninja

## Hot-path optimisations (Mar 2026)

| # | Change | Where |
|---|---|---|
| 2 | `kevent` wait clamped to 1 ms minimum — prevents busy-spin at idle | `PollerKqueue.cpp` |
| 3 | `sweepTimeouts` throttled to once per 100 ms when > 1000 clients | `ServerBase.h` |
| 4 | `EV_DISABLE` instead of `EV_DELETE` for Writable toggle; flat byte array replaces `unordered_map` in `wait()` merge | `PollerKqueue.cpp` |
| 5 | `clients_` hash map replaced with `clientSlots_[]` sparse array + `clientFds_[]` dense list — O(1) insert / lookup / erase, no hashing | `ServerBase.h` |
| 6 | `HttpClientState::response` (owned `std::string`) replaced with `responseView` (`string_view`) + `responseBuf` — static responses are zero-copy views into pre-built strings | `HttpPollServer.h`, `http_poll_server.cpp` |

## Project Structure

```
aiSocks/
├── CMakeLists.txt
├── lib/
│   ├── include/
│   │   ├── Socket.h            # Public socket API
│   │   ├── TcpSocket.h
│   │   ├── Poller.h
│   │   ├── ServerBase.h        # Poll-driven server template
│   │   └── HttpPollServer.h    # HTTP/1.x server base class
│   └── src/
│       ├── SocketImpl.cpp      # Platform-specific socket implementation
│       ├── PollerKqueue.cpp    # kqueue backend (macOS/BSD)
│       └── PollerEpoll.cpp     # epoll backend (Linux)
└── examples/
    ├── http_poll_server.cpp    # Production HTTP server example
    └── ...
```

## Building

### Debug (default)
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_EXAMPLES=ON -G Ninja
cmake --build build --target http_server
```

### Release
```bash
cmake -S . -B build-mac-rel -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON -G Ninja
cmake --build build-mac-rel --target http_server
```

### With sanitizers
```bash
# AddressSanitizer
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DBUILD_EXAMPLES=ON -G Ninja

# MemorySanitizer
cmake -S . -B build-msan -DCMAKE_BUILD_TYPE=Debug -DENABLE_MSAN=ON -DBUILD_EXAMPLES=ON -G Ninja

# UndefinedBehaviorSanitizer
cmake -S . -B build-ubsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_UBSAN=ON -DBUILD_EXAMPLES=ON -G Ninja
```

### Windows
```powershell
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --config Release
```

## Running the HTTP server

```bash
./build-mac-rel/http_server
# === Poll-Driven HTTP Server ===
# Built: Mar  1 2026 05:31:23  |  OS: macOS  |  Build: Release
# Listening on 0.0.0.0:8080
```

Stress test:
```bash
ulimit -n 65536
wrk -t12 -c5000 -d30s -H "Connection: keep-alive" http://localhost:8080/
```

## HttpPollServer — usage

Derive from `HttpPollServer` and implement `buildResponse()`:

```cpp
#include "HttpPollServer.h"
using namespace aiSocks;

class MyServer : public HttpPollServer {
public:
    explicit MyServer(const ServerBind& b) : HttpPollServer(b) {}
protected:
    void buildResponse(HttpClientState& s) override {
        // Zero-copy: point responseView at a long-lived std::string.
        // For dynamic content, write into s.responseBuf and set responseView = s.responseBuf.
        static const std::string ok =
            "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nHello!";
        s.responseView = ok;
    }
};

int main() {
    MyServer srv(ServerBind{"0.0.0.0", Port{8080}});
    srv.run(ClientLimit::Unlimited, Milliseconds{0});
}
```

## ServerBase<T> — lower-level usage

```cpp
#include "ServerBase.h"
using namespace aiSocks;

struct MyState { std::string inbuf; };

class MyServer : public ServerBase<MyState> {
public:
    explicit MyServer(const ServerBind& b) : ServerBase(b) {}
protected:
    ServerResult onReadable(TcpSocket& sock, MyState& s) override {
        char buf[4096];
        int n = sock.receive(buf, sizeof(buf));
        if (n <= 0) return ServerResult::Disconnect;
        s.inbuf.append(buf, n);
        return ServerResult::KeepConnection;
    }
    ServerResult onWritable(TcpSocket& sock, MyState& s) override {
        return ServerResult::KeepConnection;
    }
};
```

## Socket API

```cpp
#include "Socket.h"
using namespace aiSocks;

// Server
Socket srv(SocketType::TCP, AddressFamily::IPv4);
srv.setReuseAddress(true);
srv.bind("0.0.0.0", 8080);
srv.listen(128);
auto client = srv.accept();

// Client
Socket cli(SocketType::TCP, AddressFamily::IPv4);
cli.connect("127.0.0.1", 8080);
cli.send("Hello!", 6);
```

Key methods: `bind`, `listen`, `accept`, `connect`, `send`, `receive`, `setBlocking`, `setReuseAddress`, `setTimeout`, `close`, `isValid`, `getLastError`.

## Requirements

- CMake 3.15+
- C++17 compiler: MSVC 2017+, GCC 7+, Clang 5+

## License

Personal academic project.

