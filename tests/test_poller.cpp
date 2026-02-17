// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
// Tests: Poller — platform-native readiness notification (kqueue/epoll/WSAPoll)

#include "Poller.h"
#include "Socket.h"
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
        REQUIRE(true); // reached here → no exception thrown
    } catch (const SocketException& e) {
        REQUIRE_MSG(false, std::string("SocketException: ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// Test 2: add / remove does not crash with a valid socket.
// ---------------------------------------------------------------------------
static void test_poller_add_remove() {
    BEGIN_TEST("Poller: add/remove a server socket without error");
    Socket srv(SocketType::TCP, AddressFamily::IPv4);
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
    Socket srv(SocketType::TCP, AddressFamily::IPv4);
    srv.setReuseAddress(true);
    REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT + 1}));
    REQUIRE(srv.listen(5));

    Poller p;
    REQUIRE(p.add(srv, PollEvent::Readable));

    auto results = p.wait(Milliseconds{50}); // 50 ms — nobody connects
    REQUIRE(results.empty());
}

// ---------------------------------------------------------------------------
// Test 4: wait() fires when a client connects; accept/send/recv round-trip.
// ---------------------------------------------------------------------------
static void test_poller_readable_on_connect() {
    BEGIN_TEST("Poller: server socket fires Readable when client connects");
    Socket srv(SocketType::TCP, AddressFamily::IPv4);
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
        Socket c(SocketType::TCP, AddressFamily::IPv4);
        if (!c.connectTo("127.0.0.1", Port{BASE_PORT + 2}, Milliseconds{500})) {
            return;
        }
        clientConnected = true;

        // Receive "hello" from the server.
        char buf[64]{};
        int n = c.receive(buf, sizeof(buf) - 1);
        if (n > 0) clientReceived.assign(buf, static_cast<size_t>(n));
        clientDone = true;
    });

    // wait() should fire within 200 ms once the client connects.
    auto results = p.wait(Milliseconds{200});
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
// Test 5: remove() — server no longer fires after deregistration.
// ---------------------------------------------------------------------------
static void test_poller_remove_stops_events() {
    BEGIN_TEST("Poller: removed socket no longer fires");
    Socket srv(SocketType::TCP, AddressFamily::IPv4);
    srv.setReuseAddress(true);
    REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT + 3}));
    REQUIRE(srv.listen(5));

    Poller p;
    REQUIRE(p.add(srv, PollEvent::Readable));
    REQUIRE(p.remove(srv));

    // Connect a client — the poller should NOT see it (srv was removed).
    std::thread clientThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        Socket c(SocketType::TCP, AddressFamily::IPv4);
        c.connectTo("127.0.0.1", Port{BASE_PORT + 3}, Milliseconds{200});
    });

    auto results = p.wait(Milliseconds{80}); // short wait — no events expected
    clientThread.join();
    REQUIRE(results.empty());

    // Clean up the pending connection.
    srv.accept();
}

// ---------------------------------------------------------------------------
// Test 6: sendAll sends all bytes on a connected socket.
// ---------------------------------------------------------------------------
static void test_send_all() {
    BEGIN_TEST("sendAll: transmits all bytes in a single call");
    Socket srv(SocketType::TCP, AddressFamily::IPv4);
    srv.setReuseAddress(true);
    REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT + 4}));
    REQUIRE(srv.listen(1));

    std::string received;
    std::atomic<bool> done{false};
    std::thread clientThread([&]() {
        Socket c(SocketType::TCP, AddressFamily::IPv4);
        if (!c.connectTo("127.0.0.1", Port{BASE_PORT + 4}, Milliseconds{500}))
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
    BEGIN_TEST("waitReadable/waitWritable: writable fires immediately on send buffer");
    Socket srv(SocketType::TCP, AddressFamily::IPv4);
    srv.setReuseAddress(true);
    REQUIRE(srv.bind("127.0.0.1", Port{BASE_PORT + 5}));
    REQUIRE(srv.listen(1));

    std::thread clientThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        Socket c(SocketType::TCP, AddressFamily::IPv4);
        c.connectTo("127.0.0.1", Port{BASE_PORT + 5}, Milliseconds{500});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });

    auto conn = srv.accept();
    REQUIRE(conn != nullptr);
    if (conn) {
        // A freshly accepted connected socket should be writable immediately.
        REQUIRE(conn->waitWritable(Milliseconds{200}));
    }
    clientThread.join();

    // waitReadable should time out on a socket with no data pending.
    Socket lonely(SocketType::TCP, AddressFamily::IPv4);
    lonely.setReuseAddress(true);
    REQUIRE(lonely.bind("127.0.0.1", Port{BASE_PORT + 6}));
    REQUIRE(lonely.listen(1));
    bool timedOut = !lonely.waitReadable(Milliseconds{30});
    REQUIRE(timedOut);
    REQUIRE(lonely.getLastError() == SocketError::Timeout);
}

// ---------------------------------------------------------------------------
// Test 8: setLingerAbort does not fail on a valid socket.
// ---------------------------------------------------------------------------
static void test_set_linger_abort() {
    BEGIN_TEST("setLingerAbort: succeeds on a valid socket");
    Socket s(SocketType::TCP, AddressFamily::IPv4);
    REQUIRE(s.setLingerAbort(true));
    REQUIRE(s.setLingerAbort(false));
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

    return test_summary();
}
