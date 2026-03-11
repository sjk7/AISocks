# Port{0} Refactor Plan — Remaining 4 Test Files

**Goal:** Replace all `BASE_PORT + N` / `BASE + N` hardcoded ports with `Port{0}` +
`getLocalEndpoint()` so tests never collide with other processes or parallel test runs.

**Reference implementation:** `tests/test_construction.cpp` (already done, commit `383587e`).

**Key pattern:**
```cpp
// Bind in the MAIN thread (synchronous), then spawn the accept thread.
auto srv = TcpSocket::createRaw();
REQUIRE(srv.setReuseAddress(true));
REQUIRE(srv.bind("127.0.0.1", Port{0}));
REQUIRE(srv.listen(1));
auto port = srv.getLocalEndpoint().value().port; // OS-assigned ephemeral port

std::thread t([&]() {
    auto conn = srv.accept(); // safe — bind already done above
    ...
});
auto c = TcpSocket::createRaw();
REQUIRE(c.connect("127.0.0.1", port));
t.join();
```

When the server **must** live in a background thread (e.g. `server_send()` helper),
use `std::atomic<uint16_t>` to publish the port before entering `accept()`:
```cpp
std::atomic<uint16_t> portOut{0};
std::thread srvThread([&]() {
    auto srv = TcpSocket::createRaw();
    if (!srv.bind("127.0.0.1", Port{0}) || !srv.listen(1)) return;
    portOut = srv.getLocalEndpoint().value().port.value(); // publish port
    auto client = srv.accept();                            // then block
    ...
});
// Caller waits for portOut != 0 before connecting
auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
while (portOut.load() == 0 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
REQUIRE(c.connect("127.0.0.1", Port{portOut.load()}));
```

**After every file:** build (`cmake --build build_Mac_arm --config Debug`) and run
(`ctest --test-dir build_Mac_arm`) before committing.

---

## File 1: `tests/test_loopback_tcp.cpp`

**Constant to remove:** `static const uint16_t BASE_PORT = 19400;`

### Step 1 — Refactor `server_send()` helper (line 26)

Current signature:
```cpp
static void server_send(Port port, const std::string& payload, std::atomic<bool>& ready)
```

**New signature** (use `std::atomic<uint16_t>&` to publish the port):
```cpp
static void server_send(std::atomic<uint16_t>& portOut, const std::string& payload) {
    auto srv = TcpSocket::createRaw();
    REQUIRE(srv.setReuseAddress(true));
    if (!srv.bind("127.0.0.1", Port{0}) || !srv.listen(1)) return;
    auto ep = srv.getLocalEndpoint();
    if (!ep.isSuccess()) return;
    portOut = ep.value().port.value(); // publish before blocking on accept()
    auto client = srv.accept();
    if (client) {
        size_t sent = 0;
        while (sent < payload.size()) {
            int r = client->send(payload.data() + sent, payload.size() - sent);
            if (r <= 0) break;
            sent += static_cast<size_t>(r);
        }
        client->close();
    }
}
```

Remove `#include <atomic>` only if it is no longer needed elsewhere (it will still be
needed by the inline `std::atomic<uint16_t>` variables at each call site, so keep it).

### Step 2 — Test 1: "Client can connect to a listening server" (~line 57)

Server is already bound in **main thread** before spawning the client thread.

```cpp
BEGIN_TEST("Client can connect to a listening server");
{
    auto srv = TcpSocket::createRaw();
    REQUIRE(srv.setReuseAddress(true));
    REQUIRE(srv.bind("127.0.0.1", Port{0}));
    REQUIRE(srv.listen(1));
    auto port = srv.getLocalEndpoint().value().port;

    std::thread t([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto c = TcpSocket::createRaw();
        (void)c.connect("127.0.0.1", port);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    });

    auto accepted = srv.accept();
    if (accepted == nullptr) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        accepted = srv.accept();
    }
    t.join();
    REQUIRE(accepted != nullptr);
}
```

### Step 3 — Test 2: "Server can send data, client receives it exactly" (~line 80)

Uses `server_send` helper — update call site:

```cpp
BEGIN_TEST("Server can send data, client receives it exactly");
{
    const std::string message = "Hello, aiSocks!";
    std::atomic<uint16_t> portOut{0};

    std::thread srvThread([&]() { server_send(portOut, message); });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (portOut.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto client = TcpSocket::createRaw();
    bool connected = client.connect("127.0.0.1", Port{portOut.load()});
    if (!connected) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        connected = client.connect("127.0.0.1", Port{portOut.load()});
    }
    REQUIRE(connected);

    std::string received;
    recv_all(client, received);
    srvThread.join();
    REQUIRE(received == message);
}
```

### Step 4 — Test 3: "Large payload is transferred completely" (~line 109)

Same `server_send` pattern — replace `ready` atomic:

```cpp
const std::string payload(1 * 1024 * 1024, 'Z');
std::atomic<uint16_t> portOut{0};

std::thread srvThread([&]() { server_send(portOut, payload); });

auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
while (portOut.load() == 0 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

auto client = TcpSocket::createRaw();
REQUIRE(client.connect("127.0.0.1", Port{portOut.load()}));
...
```

### Step 5 — Test 4: "Client can send to server and server echoes back" (~line 135)

Server is inside a thread using `std::atomic<bool> ready`. Replace with `std::atomic<uint16_t> portOut`:

```cpp
std::atomic<uint16_t> portOut{0};

std::thread srvThread([&]() {
    auto srv = TcpSocket::createRaw();
    REQUIRE(srv.setReuseAddress(true));
    if (!srv.bind("127.0.0.1", Port{0}) || !srv.listen(1)) return;
    auto ep = srv.getLocalEndpoint();
    if (!ep.isSuccess()) return;
    portOut = ep.value().port.value();
    auto c = srv.accept();
    if (c) {
        char buf[256] = {};
        int r = c->receive(buf, sizeof(buf) - 1);
        if (r > 0) c->send(buf, static_cast<size_t>(r));
        c->close();
    }
});

auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
while (portOut.load() == 0 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

auto client = TcpSocket::createRaw();
REQUIRE(client.connect("127.0.0.1", Port{portOut.load()}));
...
```

### Step 6 — Test 5: "setReuseAddress allows rapid re-bind on same port" (~line 184)

**Special case** — this test deliberately re-binds the **same** port after close.
The trick: bind `Port{0}` first, capture the OS-assigned port, then re-bind that specific port.

```cpp
BEGIN_TEST("setReuseAddress allows rapid re-bind on same port");
{
    Port capturedPort{0};
    // First server: bind to ephemeral port, capture it, then close
    {
        auto srv = TcpSocket::createRaw();
        REQUIRE(srv.setReuseAddress(true));
        REQUIRE(srv.bind("127.0.0.1", Port{0}));
        REQUIRE(srv.listen(1));
        auto ep = srv.getLocalEndpoint();
        REQUIRE(ep.isSuccess());
        capturedPort = ep.value().port;
        srv.close();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    // Second server: re-bind same port (the whole point of the test)
    auto srv2 = TcpSocket::createRaw();
    REQUIRE(srv2.setReuseAddress(true));
    REQUIRE(srv2.bind("127.0.0.1", capturedPort));
}
```

### Step 7 — Test 6: "IPv6 loopback send/receive works" (~line 211)

Server is in a thread. Replace `ready` with `portOut`:

```cpp
std::atomic<uint16_t> portOut{0};

std::thread srvThread([&]() {
    auto srv = TcpSocket::createRaw(AddressFamily::IPv6);
    REQUIRE(srv.setReuseAddress(true));
    if (!srv.bind("::1", Port{0}) || !srv.listen(1)) {
        portOut = 1; // non-zero signals failure (client will connect and likely fail)
        return;
    }
    auto ep = srv.getLocalEndpoint();
    if (!ep.isSuccess()) { portOut = 1; return; }
    portOut = ep.value().port.value();
    auto client = srv.accept();
    if (client) {
        size_t sent = 0;
        while (sent < message.size()) {
            int r = client->send(message.data() + sent, message.size() - sent);
            if (r <= 0) break;
            sent += static_cast<size_t>(r);
        }
        client->close();
    }
});

auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
while (portOut.load() == 0 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

auto client = TcpSocket::createRaw(AddressFamily::IPv6);
bool connected = client.connect("::1", Port{portOut.load()});
srvThread.join();
...
```

### Step 8 — Tests 7 & 8: "receiveAll reads exactly N bytes" and "receiveAll returns false on premature EOF" (~lines 249, 296)

Both follow the same pattern. Replace `std::atomic<bool> ready` + `Port{BASE_PORT + 6/7}` with `std::atomic<uint16_t> portOut`:

```cpp
std::atomic<uint16_t> portOut{0};
std::thread srvThread([&]() {
    auto srv = TcpSocket::createRaw();
    REQUIRE(srv.setReuseAddress(true));
    if (!srv.bind("127.0.0.1", Port{0}) || !srv.listen(1)) return;
    auto ep = srv.getLocalEndpoint();
    if (!ep.isSuccess()) return;
    portOut = ep.value().port.value();
    auto cli = srv.accept();
    ...
});

auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
while (portOut.load() == 0 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

auto client = TcpSocket::createRaw();
REQUIRE(client.connect("127.0.0.1", Port{portOut.load()}));
```

### Step 9 — Remove `BASE_PORT` constant

Delete: `static const uint16_t BASE_PORT = 19400;`

---

## File 2: `tests/test_poller.cpp`

**Constant to remove:** `static const uint16_t BASE_PORT = 19600;`

The good news: every test in this file binds the server in the **main test function**
before spawning any client threads. The refactor is uniform — bind `Port{0}`, call
`getLocalEndpoint()` immediately, pass the port to wherever `BASE_PORT + N` was used.

### Step 1 — `test_poller_add_remove` (no client thread)

```cpp
auto srv = TcpSocket::createRaw();
REQUIRE(srv.setReuseAddress(true));
REQUIRE(srv.bind("127.0.0.1", Port{0}));
REQUIRE(srv.listen(5));
// port not needed here — just remove BASE_PORT + 0
```

### Step 2 — `test_poller_timeout` (no client)

```cpp
REQUIRE(srv.bind("127.0.0.1", Port{0}));
// no port capture needed
```

### Step 3 — `test_poller_readable_on_connect` (client thread connects)

```cpp
REQUIRE(srv.bind("127.0.0.1", Port{0}));
REQUIRE(srv.listen(5));
auto port = srv.getLocalEndpoint().value().port;

std::thread clientThread([port, &clientConnected, &clientReceived, &clientDone]() {
    // replace Port{BASE_PORT + 2} with port in both connect() calls
    ...
    if (!c.connect("127.0.0.1", port, Milliseconds{500})) return;
    ...
});
```

### Step 4 — `test_poller_remove_stops_events` (client thread connects)

```cpp
REQUIRE(srv.bind("127.0.0.1", Port{0}));
REQUIRE(srv.listen(5));
auto port = srv.getLocalEndpoint().value().port;

std::thread clientThread([port]() {
    ...
    (void)c.connect("127.0.0.1", port, Milliseconds{200});
});
```

### Step 5 — `test_send_all` (client thread connects)

```cpp
REQUIRE(srv.bind("127.0.0.1", Port{0}));
REQUIRE(srv.listen(1));
auto port = srv.getLocalEndpoint().value().port;

std::thread clientThread([port, &received, &done]() {
    if (!c.connect("127.0.0.1", port, Milliseconds{500})) return;
    ...
});
```

### Step 6 — `test_wait_readable_writable` (two servers)

First server has a client thread; second is solo:

```cpp
// First server (Port{BASE_PORT + 5}):
REQUIRE(srv.bind("127.0.0.1", Port{0}));
REQUIRE(srv.listen(1));
auto port5 = srv.getLocalEndpoint().value().port;

std::thread clientThread([port5]() {
    ...
    (void)c.connect("127.0.0.1", port5, Milliseconds{500});
    ...
});
...

// Second server (Port{BASE_PORT + 6}) — no client, just waitReadable test:
REQUIRE(lonely.bind("127.0.0.1", Port{0}));
REQUIRE(lonely.listen(1));
// port not needed
```

### Step 7 — `test_poller_async_connect` (immediate connect in main)

```cpp
REQUIRE(srv.bind("127.0.0.1", Port{0}));
REQUIRE(srv.listen(5));
auto port = srv.getLocalEndpoint().value().port;

// Replace both occurrences of Port{BASE_PORT + 7} with port:
bool immediateSuccess = client.connect("127.0.0.1", port);
```

### Step 8 — Remove `BASE_PORT` constant

Delete: `static const uint16_t BASE_PORT = 19600;`

---

## File 3: `tests/test_error_messages.cpp`

**Constant to remove:** `static constexpr uint16_t BASE = 21000;`

This file has a mix of intentional-failure tests (where the port is irrelevant) and
real connection tests (where it matters).

### Step 1 — `test_bind_exception_message` — double-bind test

The test intentionally tries to bind the **same port twice without reuseAddr**.
Pattern: bind occupant to `Port{0}`, capture port, then try to bind same port again.

```cpp
BEGIN_TEST("ServerBind error: basic error handling");
{
    TcpSocket occupant(AddressFamily::IPv4,
        ServerBind{"127.0.0.1", Port{0}, Backlog{5}, false});
    auto ep = occupant.getLocalEndpoint();
    REQUIRE(ep.isSuccess());
    Port occupiedPort = ep.value().port;

    auto result = SocketFactory::createTcpServer(AddressFamily::IPv4,
        ServerBind{"127.0.0.1", occupiedPort, Backlog{5}, false});
    REQUIRE(result.isError());
    REQUIRE(result.error() != SocketError::None);
}

BEGIN_TEST("ServerBind error: error() == BindFailed");
{
    TcpSocket occupant(AddressFamily::IPv4,
        ServerBind{"127.0.0.1", Port{0}, Backlog{5}, false});
    auto ep = occupant.getLocalEndpoint();
    REQUIRE(ep.isSuccess());
    Port occupiedPort = ep.value().port;

    auto result = SocketFactory::createTcpServer(AddressFamily::IPv4,
        ServerBind{"127.0.0.1", occupiedPort, Backlog{5}, false});
    SocketError code = result.error();
    REQUIRE(code == SocketError::BindFailed);
}
```

### Step 2 — DNS tests (Port{BASE + 10})

These connect to a non-existent hostname — DNS fails before the port matters at all.
Replace `Port{BASE + 10}` with `Port{80}` (a harmless well-known port):

```cpp
ConnectArgs{BAD_HOST, Port{80}, Milliseconds{50}}
```

Apply in both places inside `test_dns_error_message`.
Also `(void)s.connect(BAD_HOST, Port{80}, Milliseconds{50});`

### Step 3 — Closed-socket tests (Port{BASE + 20})

The socket is closed **before** the operation — the port value is irrelevant.
Replace all `Port{BASE + 20}` with `Port{1}`:

```cpp
REQUIRE(!s.bind("127.0.0.1", Port{1}));
REQUIRE(!s.connect("127.0.0.1", Port{1}));
```

### Step 4 — UDP closed-socket test (Port{BASE + 30})

Same — closed socket, port irrelevant. Replace `Port{BASE + 30}` with `Port{1}`:

```cpp
Endpoint dest{"127.0.0.1", Port{1}, AddressFamily::IPv4};
```

### Step 5 — `test_error_clears_on_success` (Port{BASE + 40})

Server binds in **main thread**, then spawns accept thread. Pattern:

```cpp
auto srv = TcpSocket::createRaw();
REQUIRE(srv.setReuseAddress(true));
REQUIRE(srv.bind("127.0.0.1", Port{0}));
REQUIRE(srv.listen(1));
auto port = srv.getLocalEndpoint().value().port;

std::thread t([&]() {
    auto peer = srv.accept();
    if (peer) peer->close();
});

auto c = TcpSocket::createRaw();
(void)c.connect("127.0.0.1", Port{1}, Milliseconds{100}); // refused (leave as-is)
...
auto c2 = TcpSocket::createRaw();
REQUIRE(c2.connect("127.0.0.1", port));
...
```

### Step 6 — `test_post_shutdown_errors` (Port{BASE + 50})

Server binds in main thread, spawns accept thread. Same pattern:

```cpp
auto srv = TcpSocket::createRaw();
REQUIRE(srv.setReuseAddress(true));
REQUIRE(srv.bind("127.0.0.1", Port{0}));
REQUIRE(srv.listen(1));
auto port = srv.getLocalEndpoint().value().port;

std::thread t([&]() { (void)srv.accept(); });

auto c = TcpSocket::createRaw();
REQUIRE(c.connect("127.0.0.1", port));
...
```

### Step 7 — Remove `BASE` constant

Delete: `static constexpr uint16_t BASE = 21000;`

Also remove `#include <atomic>` if no more atomics are used in this file after the
refactor. (There are no remaining `std::atomic` uses in `test_error_messages.cpp` after
these changes — so remove the include.)

---

## File 4: `tests/test_new_features.cpp`

**Constant to remove:** `static constexpr int BASE = 20000;`

This is the largest file (772 lines). Work through section by section.

### Step 1 — `test_endpoints()` sub-test 1: "address and port correct after bind"

Old check: `e.port == Port{BASE} && e.address == "127.0.0.1"`
New: bind `Port{0}`, check port is non-zero and address matches:

```cpp
auto s = TcpSocket::createRaw();
REQUIRE(s.setReuseAddress(true));
REQUIRE(s.bind("127.0.0.1", Port{0}));
auto ep = s.getLocalEndpoint();
REQUIRE(ep.isSuccess());
if (!ep) return;
const auto& e = ep.value();
REQUIRE(e.port != Port{0} && e.address == "127.0.0.1"
    && e.family == AddressFamily::IPv4);
```

### Step 2 — `test_endpoints()` sub-test 3: "getPeerEndpoint: populated after TCP connect"

Server binds `Port{BASE + 1}` in **main thread** before spawning accept thread:

```cpp
auto srv = TcpSocket::createRaw();
REQUIRE(srv.setReuseAddress(true));
REQUIRE(srv.bind("127.0.0.1", Port{0}));
REQUIRE(srv.listen(1));
auto srvPort = srv.getLocalEndpoint().value().port;

std::thread t([&]() {
    ...
    auto peer = srv.accept();
    ...
});

...
bool connected = c.connect("127.0.0.1", srvPort);
...
auto& e = ep.value();
REQUIRE(e.port == srvPort && e.address == "127.0.0.1");
```

Note: change the `e.port == Port{BASE + 1}` check to `e.port == srvPort`.

### Step 3 — `test_udp()` — basic UDP loopback (Port{BASE + 10})

```cpp
UdpSocket receiver;
REQUIRE(receiver.setReuseAddress(true));
REQUIRE(receiver.bind("127.0.0.1", Port{0}));
REQUIRE(receiver.setReceiveTimeout(Milliseconds{2000}));
auto ep = receiver.getLocalEndpoint();
REQUIRE(ep.isSuccess());
Port receiverPort = ep.value().port;

Endpoint dest{"127.0.0.1", receiverPort, AddressFamily::IPv4};
```

### Step 4 — `test_udp()` — multiple datagrams (Port{BASE + 11})

Same pattern as Step 3. Bind `Port{0}`, get port, build `Endpoint dest`.

### Step 5 — `test_udp_connected()` (Port{BASE + 30})

```cpp
UdpSocket server;
REQUIRE(server.setReuseAddress(true));
REQUIRE(server.bind("127.0.0.1", Port{0}));
REQUIRE(server.setReceiveTimeout(Milliseconds{2000}));
auto ep = server.getLocalEndpoint();
REQUIRE(ep.isSuccess());
Port serverPort = ep.value().port;

REQUIRE(client.connect("127.0.0.1", serverPort));
auto peer = client.getPeerEndpoint();
REQUIRE(peer.isSuccess() && peer.value().port == serverPort); // was Port{BASE+30}
```

### Step 6 — `test_udp_transfer()` — all three sub-tests (Port{BASE+50}, +51, +52)

Each sub-test creates an independent receiver. Apply same pattern (bind `Port{0}`, get
port, build `Endpoint dest`) to all three:

- "20 datagrams" — `Port{BASE + 50}` → port from `srv.getLocalEndpoint()`
- "bidirectional echo" — `Port{BASE + 51}` → port from `srv.getLocalEndpoint()`
- "8192-byte datagram" — `Port{BASE + 52}` → port from `srv.getLocalEndpoint()`

For the bidirectional echo test, `srvAddr` is the `Endpoint` passed to `sendTo`.
Build it after `getLocalEndpoint()`:
```cpp
Endpoint srvAddr{"127.0.0.1", srv.getLocalEndpoint().value().port, AddressFamily::IPv4};
```

### Step 7 — `test_bulk_throughput()` — UDP (Port{BASE + 60})

UDP receiver is in main scope. Bind `Port{0}`, get port, build `dest`:

```cpp
REQUIRE(srv.bind("127.0.0.1", Port{0}));
...
auto srvEp = srv.getLocalEndpoint();
REQUIRE(srvEp.isSuccess());
Endpoint dest{"127.0.0.1", srvEp.value().port, AddressFamily::IPv4};
```

### Step 8 — `test_bulk_throughput()` — TCP (Port{BASE + 61})

This is the tricky one: the server lives entirely inside a background thread and uses
`std::atomic<bool> ready`. Replace with `std::atomic<uint16_t> portOut{0}`:

```cpp
std::atomic<uint16_t> portOut{0};

std::thread srvThread([&]() {
    auto srv = TcpSocket::createRaw();
    REQUIRE(srv.setReuseAddress(true));
    if (!srv.bind("127.0.0.1", Port{0}) || !srv.listen(1)) {
        portOut = 1; // signal failure (non-zero)
        return;
    }
    auto ep = srv.getLocalEndpoint();
    if (!ep.isSuccess()) { portOut = 1; return; }
    portOut = ep.value().port.value(); // publish port, THEN enter accept()
    auto peer = srv.accept();
    ...
});

auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
while (portOut.load() == 0 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

auto c = TcpSocket::createRaw();
REQUIRE(c.connect("127.0.0.1", Port{portOut.load()}));
```

### Step 9 — `test_span_overloads()` — TCP (Port{BASE + 40})

Server binds in main, spawns accept thread. Standard pattern:

```cpp
REQUIRE(srv.bind("127.0.0.1", Port{0}));
REQUIRE(srv.listen(1));
auto port = srv.getLocalEndpoint().value().port;

std::thread t([&]() { ... }); // accept thread — no port reference needed inside

REQUIRE(cli.connect("127.0.0.1", port));
```

### Step 10 — `test_span_overloads()` — UDP (Port{BASE + 41})

```cpp
REQUIRE(receiver.bind("127.0.0.1", Port{0}));
REQUIRE(receiver.setReceiveTimeout(Milliseconds{2000}));
auto ep = receiver.getLocalEndpoint();
REQUIRE(ep.isSuccess());
Endpoint dest{"127.0.0.1", ep.value().port, AddressFamily::IPv4};
```

### Step 11 — `test_shutdown()` — Port{BASE + 20} and Port{BASE + 21}

Both servers bind in main thread:

```cpp
// shutdown(Write) test:
REQUIRE(srv.bind("127.0.0.1", Port{0}));
REQUIRE(srv.listen(1));
auto port20 = srv.getLocalEndpoint().value().port;
...
REQUIRE(c.connect("127.0.0.1", port20));

// shutdown(Both) test:
REQUIRE(srv.bind("127.0.0.1", Port{0}));
REQUIRE(srv.listen(1));
auto port21 = srv.getLocalEndpoint().value().port;
...
REQUIRE(c.connect("127.0.0.1", port21));
```

### Step 12 — Remove `BASE` constant

Delete: `static constexpr int BASE = 20000;`

---

## Build and Test

After completing each file, or all four files together:

```sh
cmake --build build_Mac_arm --config Debug
ctest --test-dir build_Mac_arm --output-on-failure
```

All 38 tests should pass.

---

## Commit

```sh
git add tests/test_loopback_tcp.cpp tests/test_poller.cpp \
        tests/test_error_messages.cpp tests/test_new_features.cpp
git commit -m "tests: replace BASE+N hardcoded ports with Port{0} + getLocalEndpoint()"
git push
```

---

## Edge Cases to Watch

| Test | Issue | Resolution |
|---|---|---|
| `setReuseAddress allows rapid re-bind` | Must re-bind the **same** port | Bind Port{0}, capture port, close, re-bind captured port |
| `ServerBind error: error() == BindFailed` | Must collide on same port | `occupant` binds Port{0}, captures port, second bind uses same port |
| DNS tests (`Port{BASE+10}`) | Remote port doesn't matter (DNS fails first) | Use Port{80} as a harmless constant |
| Closed-socket tests (`Port{BASE+20}`, +30) | Socket closed before op; port irrelevant | Use Port{1} |
| TCP bulk throughput server in thread | Thread pattern; needs atomic port signal | `std::atomic<uint16_t> portOut{0}` |
| IPv6 server in thread | Bind may fail (IPv6 not available) | Signal portOut = 1 on failure to unblock client |
