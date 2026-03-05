# aiSocks

> **Note:** This project was created entirely by AI (GitHub Copilot / Claude) under my direct supervision. The code is mine, but not physically typed by me.

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
| 6 | `HttpClientState::response` (owned `std::string`) replaced with `responseView` (`string_view`) + `responseBuf` — static responses are zero-copy views into pre-built strings | `HttpPollServer.h`, `low_level_http_server.cpp` |

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
    ├── low_level_http_server.cpp    # Level 1: Manual HTTP response building
    ├── simple_file_server.cpp       # Level 2: Basic HttpFileServer usage (~50 lines)
    ├── advanced_file_server.cpp     # Level 3: Extends HttpFileServer (auth, logging)
    └── ...
```

## Building

### Release with Debug Info (Recommended)

#### Windows (MSVC)
```powershell
cmake -S . -B build-release -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build-release --config RelWithDebInfo --parallel
```

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

### Debug Build

#### Windows (MSVC)
```powershell
cmake -S . -B build-debug -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build-debug --config Debug --parallel
```

#### macOS & Linux
```bash
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build-debug --parallel
```

### With Sanitizers

#### AddressSanitizer (Linux/macOS)
```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build-asan --parallel
```

#### MemorySanitizer (Linux only)
```bash
cmake -S . -B build-msan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_MSAN=ON -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build-msan --parallel
```

#### UndefinedBehaviorSanitizer (Linux/macOS)
```bash
cmake -S . -B build-ubsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_UBSAN=ON -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build-ubsan --parallel
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

### Parallel Test Execution
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

# Windows
ctest --output-on-failure -R "test_tcp_socket" -C RelWithDebInfo
```

### List Available Tests
```bash
ctest --show-only

# Windows
ctest --show-only -C RelWithDebInfo
```

### Test Output
```
Test project /path/to/build-release
    Start  1: test_socket_basics
1/22 Test #1: test_socket_basics ...............   Passed    0.02 sec
    Start  2: test_ip_utils
2/22 Test #2: test_ip_utils ....................   Passed    0.03 sec
...
100% tests passed, 0 tests failed out of 22

Total Test time (real) =   8.74 sec
```

### Slow Tests
The `test_timeout_heap` test is disabled by default (takes ~20 seconds). Enable with:
```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DALLOW_SLOW_TESTS=ON
cmake --build build-release --parallel
ctest --output-on-failure
```

## Running the HTTP server

```bash
# Linux/macOS
./build-release/http_server

# Windows
.\build-release\http_server.exe

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

## Async / coroutine / executor integration

aiSocks is **intentionally single-threaded and synchronous**.  The design
tradeoff: zero overhead on the hot path, zero thread-safety surface, trivial
debugging.

### Documented extension seams

If you need to integrate with an external executor (`std::execution`,
Asio, `io_uring`, a coroutine scheduler, etc.) here are the intended hooks:

| Goal | How |
|---|---|
| Drive `run()` from an executor thread | Call `server.setHandleSignals(false)`, invoke `run()` on a dedicated thread, use `requestStop()` from any thread to signal shutdown. |
| Yield after each event | Override `onIdle()` and return `KeepConnection`; pass a bounded `timeout` to `run()` so `onIdle()` fires on a regular cadence. |
| Integrate a Poller-compatible fd with `io_uring` or `epoll` | `ServerBase` exposes `getSocket()` for the listener fd.  Register it separately with your ring; deliver readiness as calls to a subclass that bypasses `run()` and drives `drainAccept()` / `onReadable()` / `onWritable()` directly (requires friend or protected-accessor extension). |
| Per-connection coroutine | Store a coroutine handle in `ClientData`.  Resume it from `onReadable()` / `onWritable()` and return `Disconnect` when the coroutine is done. |

There is currently no built-in `io_uring` backend.  The kqueue (macOS/BSD)
and epoll (Linux) backends live entirely inside `Poller`; adding an
`io_uring` `Poller` implementation would not require changes to `ServerBase`
or `SimpleServer`.

## Testing

Comprehensive tests are included when building with `-DBUILD_TESTS=ON`. The suite covers **26+ tests**: socket operations (TCP/UDP, IPv4/IPv6, blocking/non-blocking), HTTP/1.x protocol, keep-alive connections, zero-copy responses, file serving, and utilities.

For detailed testing instructions, test descriptions, and running options, see [README_TESTS.md](README_TESTS.md).

## Requirements

- CMake 3.15+
- C++17 compiler: MSVC 2017+, GCC 7+, Clang 5+

## License

Personal academic project.

