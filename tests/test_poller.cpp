// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
// Tests: Poller  platform-native readiness notification (kqueue/epoll/WSAPoll)

#include "Poller.h"
#include "TcpSocket.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using namespace aiSocks;

static const uint16_t BASE_PORT = 19600;

// ---------------------------------------------------------------------------
// Test 1: Poller can be constructed and destroyed safely.
// ---------------------------------------------------------------------------
static void test_poller_construct() {
    BEGIN_TEST("Poller constructs and destructs without error");
    try {
        Poller p;
        REQUIRE(true); // reached here  no exception thrown
    } catch (const SocketException& e) {
        REQUIRE_MSG(false, std::string("SocketException: ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// Test 2: add / remove does not crash with a valid socket.
// ---------------------------------------------------------------------------
static void test_poller_add_remove() {
    BEGIN_TEST("Poller: add/remove a server socket without error");
    auto srv = TcpSocket::createRaw();
    srv.setReuseAddress(true);
    REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT}));
    REQUIRE(srv.listen(5));

    Poller p;
    REQUIRE(p.add(srv, PollEvent::Readable));
    REQUIRE(p.remove(srv));
}

// ---------------------------------------------------------------------------
// Test 3: wait() times out when no client connects.
// ---------------------------------------------------------------------------
static void test_poller_timeout() {
    BEGIN_TEST("Poller: wait() returns empty vector on timeout");
    auto srv = TcpSocket::createRaw();
    srv.setReuseAddress(true);
    REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT + 1}));
    REQUIRE(srv.listen(5));

    Poller p;
    REQUIRE(p.add(srv, PollEvent::Readable));

    auto results = p.wait(Milliseconds{10}); // 10 ms  nobody connects
    REQUIRE(results.empty());
}

// ---------------------------------------------------------------------------
// Test 4: wait() fires when a client connects; accept/send/recv round-trip.
// ---------------------------------------------------------------------------
static void test_poller_readable_on_connect() {
    BEGIN_TEST("Poller: server socket fires Readable when client connects");
    auto srv = TcpSocket::createRaw();
    srv.setReuseAddress(true);
    REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT + 2}));
    REQUIRE(srv.listen(5));

    Poller p;
    REQUIRE(p.add(srv, PollEvent::Readable));

    std::atomic<bool> clientConnected{false};
    std::string clientReceived;
    std::atomic<bool> clientDone{false};

    // Client thread: connect, receive one message, store it.
    std::thread clientThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        auto c = TcpSocket::createRaw();
        if (!c.connect("127.0.0.1", Port{BASE_PORT + 2}, Milliseconds{500})) {
            return;
        }
        clientConnected = true;

        // Receive "hello" from the server.
        char buf[64]{};
        int n = c.receive(buf, sizeof(buf) - 1);
        if (n > 0) clientReceived.assign(buf, static_cast<size_t>(n));
        clientDone = true;
    });

    // wait() should fire within 50 ms once the client connects.
    auto results = p.wait(Milliseconds{50});
    REQUIRE(!results.empty());
    if (!results.empty()) {
        REQUIRE(results[0].socket == &srv);
        REQUIRE(hasFlag(results[0].events, PollEvent::Readable));
    }

    // Accept the connection and send "hello".
    auto conn = srv.accept();
    REQUIRE(conn != nullptr);
    if (conn) {
        const std::string msg = "hello";
        conn->sendAll(msg.data(), msg.size());
    }

    clientThread.join();
    REQUIRE(clientConnected.load());
    REQUIRE(clientReceived == "hello");
}

// ---------------------------------------------------------------------------
// Test 5: remove()  server no longer fires after deregistration.
// ---------------------------------------------------------------------------
static void test_poller_remove_stops_events() {
    BEGIN_TEST("Poller: removed socket no longer fires");
    auto srv = TcpSocket::createRaw();
    srv.setReuseAddress(true);
    REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT + 3}));
    REQUIRE(srv.listen(5));

    Poller p;
    REQUIRE(p.add(srv, PollEvent::Readable));
    REQUIRE(p.remove(srv));

    // Connect a client  the poller should NOT see it (srv was removed).
    std::thread clientThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        auto c = TcpSocket::createRaw();
        (void)c.connect("127.0.0.1", Port{BASE_PORT + 3}, Milliseconds{200});
    });

    auto results = p.wait(Milliseconds{10}); // short wait  no events expected
    clientThread.join();
    REQUIRE(results.empty());

    // Clean up the pending connection.
    (void)srv.accept();
}

// ---------------------------------------------------------------------------
// Test 6: sendAll sends all bytes on a connected socket.
// ---------------------------------------------------------------------------
static void test_send_all() {
    BEGIN_TEST("sendAll: transmits all bytes in a single call");
    auto srv = TcpSocket::createRaw();
    srv.setReuseAddress(true);
    REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT + 4})); //-V112
    REQUIRE(srv.listen(1));

    std::string received;
    std::atomic<bool> done{false};
    std::thread clientThread([&]() {
        auto c = TcpSocket::createRaw();
        if (!c.connect(
                "127.0.0.1", Port{BASE_PORT + 4}, Milliseconds{500})) //-V112
            return;
        char buf[256]{};
        int n = c.receive(buf, sizeof(buf) - 1);
        if (n > 0) received.assign(buf, static_cast<size_t>(n));
        done = true;
    });

    auto conn = srv.accept();
    REQUIRE(conn != nullptr);
    if (conn) {
        const std::string msg = "all-bytes-sent";
        REQUIRE(conn->sendAll(msg.data(), msg.size()));
    }
    clientThread.join();
    REQUIRE(received == "all-bytes-sent");
}

// ---------------------------------------------------------------------------
// Test 7: waitReadable / waitWritable basic checks.
// ---------------------------------------------------------------------------
static void test_wait_readable_writable() {
    BEGIN_TEST(
        "waitReadable/waitWritable: writable fires immediately on send buffer");
    auto srv = TcpSocket::createRaw();
    srv.setReuseAddress(true);
    REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT + 5}));
    REQUIRE(srv.listen(1));

    std::thread clientThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        auto c = TcpSocket::createRaw();
        (void)c.connect("127.0.0.1", Port{BASE_PORT + 5}, Milliseconds{500});
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    });

    auto conn = srv.accept();
    REQUIRE(conn != nullptr);
    if (conn) {
        // A freshly accepted connected socket should be writable immediately.
        REQUIRE(conn->waitWritable(Milliseconds{50}));
    }
    clientThread.join();

    // waitReadable should time out on a socket with no data pending.
    auto lonely = TcpSocket::createRaw();
    lonely.setReuseAddress(true);
    REQUIRE(lonely.bind("127.0.0.1", Port{BASE_PORT + 6}));
    REQUIRE(lonely.listen(1));
    bool timedOut = !lonely.waitReadable(Milliseconds{10});
    REQUIRE(timedOut);
    REQUIRE(lonely.getLastError() == SocketError::Timeout);
}

// ---------------------------------------------------------------------------
// Test 8: setLingerAbort does not fail on a valid socket.
// ---------------------------------------------------------------------------
static void test_set_linger_abort() {
    BEGIN_TEST("setLingerAbort: succeeds on a valid socket");
    auto s = TcpSocket::createRaw();
    REQUIRE(s.setLingerAbort(true));
    REQUIRE(s.setLingerAbort(false));
}

// ---------------------------------------------------------------------------
// Test 9: Poller-driven async connect.
//
// Pattern:
//   1. setBlocking(false) on the client socket
//   2. connect() either:
//        a) returns true immediately (loopback can complete synchronously), or
//        b) returns false + WouldBlock (EINPROGRESS)  then Poller watches
//           the client socket for Writable to detect handshake completion
//   3. Accept  send  receive to confirm the connection is usable
// ---------------------------------------------------------------------------
static void test_poller_async_connect() {
    BEGIN_TEST("Poller: async (non-blocking) connect via Writable event");

    // Server side  accept in main thread after poller fires.
    auto srv = TcpSocket::createRaw();
    srv.setReuseAddress(true);
    REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT + 7}));
    REQUIRE(srv.listen(5));

    // Client side  non-blocking connect.
    auto client = TcpSocket::createRaw();
    REQUIRE(client.setBlocking(false));

    // connect() may return true immediately (loopback) or false + WouldBlock
    // (EINPROGRESS on a real network or slower path).
    bool immediateSuccess = client.connect("127.0.0.1", Port{BASE_PORT + 7});
    if (!immediateSuccess) {
        // Must be in-progress, not a hard failure.
        REQUIRE(client.getLastError() == SocketError::WouldBlock);

        // Watch the client for Writable = handshake complete.
        Poller p;
        REQUIRE(p.add(client, PollEvent::Writable));

        auto results = p.wait(Milliseconds{100});
        REQUIRE(!results.empty());
        bool writable = false;
        for (const auto& r : results) {
            if (r.socket == &client && hasFlag(r.events, PollEvent::Writable))
                writable = true;
        }
        REQUIRE(writable);
        REQUIRE_MSG(true, "Poller fired Writable for in-progress connect");
    } else {
        REQUIRE_MSG(true,
            "connect() completed immediately (loopback fast-path)  "
            "no Poller poll needed");
    }

    // Restore blocking mode before using the socket normally.
    REQUIRE(client.setBlocking(true));

    // Accept the connection on the server side.
    auto srvConn = srv.accept();
    REQUIRE(srvConn != nullptr);

    // Round-trip: server sends, client receives.
    if (srvConn) {
        const std::string msg = "async-connected";
        REQUIRE(srvConn->sendAll(msg.data(), msg.size()));
        char buf[64]{};
        int n = client.receive(buf, sizeof(buf) - 1);
        REQUIRE(n == static_cast<int>(msg.size()));
        REQUIRE(std::string(buf, static_cast<size_t>(n)) == msg);
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== Poller + new-feature tests ===\n";

    test_poller_construct();
    test_poller_add_remove();
    test_poller_timeout();
    test_poller_readable_on_connect();
    test_poller_remove_stops_events();
    test_send_all();
    test_wait_readable_writable();
    test_set_linger_abort();
    test_poller_async_connect();

    return test_summary();
}
