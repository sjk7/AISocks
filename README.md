# aiSocks

> **Note:** This project was created entirely by AI (GitHub Copilot / Claude) under my direct supervision. The code is mine, but not physically typed by me.

> **Note on Exceptions** The library does not throw exceptions willy-nilly. In fact, we do not throw them directly at all, currently.
> However, STL is used, so xecptions may be thrown in truly exceptional circumstances. 

Cross-platform C++17 socket library — and a high-performance poll-driven HTTP/1.x server built on top of it.

Zero dependencies beyond a standard C++17 compiler and CMake.

## Features

- ✅ Cross-platform (Windows, macOS, Linux)
- ✅ Clean pimpl API — platform headers never leak into user code
- ✅ IPv4 and IPv6
- ✅ TCP and UDP
- ✅ Blocking and non-blocking modes
- ✅ Poll-driven `ServerBase<T>` — single-thread, zero allocations in the hot path
- ✅ `HttpPollServer` — HTTP/1.x framing, keep-alive, streaming, zero-copy static responses
- ✅ Optional TLS/HTTPS client path via OpenSSL (`AISOCKS_ENABLE_TLS`)
- ✅ kqueue backend (macOS/BSD) with lazy-deletion timeout heap
- ✅ AddressSanitizer / MemorySanitizer / UBSan build presets
- ✅ Modern C++17, CMake, Ninja

## Building

### Release with Debug Info (Recommended)

#### Windows (MSVC)
```powershell
cmake -S . -B build-release -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
```

```powershell
cmake --build build-release --config RelWithDebInfo --parallel
```

#### macOS (Clang)
```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
```

```bash
cmake --build build-release --parallel
```

#### Linux (GCC or Clang)
```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
```

```bash
cmake --build build-release --parallel
```

### Debug Build

#### Windows (MSVC)
```powershell
cmake -S . -B build-debug -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
```

```powershell
cmake --build build-debug --config Debug --parallel
```

#### macOS & Linux
```bash
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
```

```bash
cmake --build build-debug --parallel
```

### With Sanitizers

#### AddressSanitizer (Linux/macOS)
```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
```

```bash
cmake --build build-asan --parallel
```

#### MemorySanitizer (Linux only)
```bash
cmake -S . -B build-msan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_MSAN=ON -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
```

```bash
cmake --build build-msan --parallel
```

#### UndefinedBehaviorSanitizer (Linux/macOS)
```bash
cmake -S . -B build-ubsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_UBSAN=ON -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
```

```bash
cmake --build build-ubsan --parallel
```

## TLS Client Defaults

- `HttpClient::Options::verifyCertificate` defaults to `true`.
- Callers should provide explicit trust configuration (`caCertFile` /
    `caCertDir`) where appropriate for their deployment.
- Verification currently covers certificate chain validation and hostname/IP
    matching against the configured trust store or system roots.
- Revocation is not checked by default. aiSocks does not enable OCSP/CRL
    fetching or hard-fail revocation policy automatically through OpenSSL.
- If your threat model requires revocation enforcement, terminate TLS in a
    proxy/load balancer that provides it, or add custom TLS integration on top
    of aiSocks.
- New-TCP TLS session resumption is not currently promised by the public
    client contract. Same-socket keep-alive reuse is supported; broader
    resumption remains follow-up work.

## Filesystem-lite API (no std::filesystem required)

For path/file operations, prefer `PathHelper` + `FileIO` instead of
`std::filesystem`.

Key helpers in `PathHelper`:
- `normalizePath(path)`
- `joinPath(base, component)`
- `createDirectories(path)`
- `removeAll(path)`
- `tempDirectory()`

This keeps compile times predictable and avoids bringing `std::filesystem`
into user code unless you explicitly choose to use it.

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
│   │   ├── HttpPollServer.h    # HTTP/1.x server base class
│   │   ├── HttpFileServer.h    # File serving with logging
│   │   └── LogRotation.h      # Log rotation support
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

## Running Tests

### All Tests
```bash
ctest --output-on-failure
```

### Parallel Test Execution
```bash
ctest --output-on-failure -j 4
```

### Specific Test
```bash
ctest --output-on-failure -R "test_tcp_socket"
```

### List Available Tests
```bash
ctest --show-only
```

## Large-file Benchmark

Use the benchmark script to compare cold/uncached and repeated large-file request timings with `advanced_file_server`:

```bash
scripts/benchmark_large_file_server.sh
```

Useful overrides:

```bash
PORT=18080 REPEATS=8 LARGE_MB=16 SMALL_KB=32 scripts/benchmark_large_file_server.sh
```

The script pre-cleans the chosen port, starts `advanced_file_server`, generates temporary benchmark assets under the configured root, prints timing summaries, and removes generated assets on exit.

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
        // Ergonomic default: provide body only.
        // Content-Type / Content-Length / Connection are filled automatically.
        respondText(s, "Hello!\n");
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

For detailed testing instructions, test descriptions, and running options, see [README_TESTS.md](README_TESTS.md).

For examples and usage patterns, see [README_EXAMPLES.md](README_EXAMPLES.md).

For log rotation configuration and usage, see [lib/LogRotation.md](lib/LogRotation.md).

## Requirements

- CMake 3.15+
- C++17 compiler: MSVC 2017+, GCC 7+, Clang 5+

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.

