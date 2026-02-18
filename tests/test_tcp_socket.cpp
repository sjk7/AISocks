// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
//
// test_tcp_socket.cpp — Happy and sad path tests for TcpSocket specifically.
//
// These tests verify the TcpSocket-typed API: that correct operations work,
// that invalid operations produce the expected errors, and that the type-safe
// design contracts hold (accept returns unique_ptr<TcpSocket>, not Socket).

#include "TcpSocket.h"
#include "test_helpers.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using namespace aiSocks;

static const uint16_t BASE = 21800; // unique port range for this test suite

// -----------------------------------------------------------------------
// Happy path: construction
// -----------------------------------------------------------------------
static void test_happy_construction() {
    BEGIN_TEST("TcpSocket: default ctor (TCP/IPv4) is valid");
    {
        TcpSocket s;
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv4);
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("TcpSocket: explicit IPv6 ctor is valid");
    {
        TcpSocket s(AddressFamily::IPv6);
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv6);
    }

    BEGIN_TEST("TcpSocket: ServerBind ctor binds and listens in one step");
    {
        bool threw = false;
        try {
            TcpSocket srv(
                AddressFamily::IPv4, ServerBind{"127.0.0.1", Port{BASE}});
            REQUIRE(srv.isValid());
            auto ep = srv.getLocalEndpoint();
            REQUIRE(ep.has_value());
            REQUIRE(ep->port.value == BASE);
        } catch (const SocketException& e) {
            std::cerr << "  Unexpected: " << e.what() << "\n";
            threw = true;
        }
        REQUIRE(!threw);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: ConnectTo ctor creates a connected socket");
    {
        std::atomic<bool> ready{false};
        std::thread srvThread([&] {
            try {
                TcpSocket srv(AddressFamily::IPv4,
                    ServerBind{"127.0.0.1", Port{BASE + 1}});
                ready = true;
                srv.accept(); // accepts and discards immediately
            } catch (...) {
                ready = true;
            }
        });
        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        bool threw = false;
        try {
            TcpSocket c(
                AddressFamily::IPv4, ConnectTo{"127.0.0.1", Port{BASE + 1}});
            REQUIRE(c.isValid());
            auto peer = c.getPeerEndpoint();
            REQUIRE(peer.has_value());
            REQUIRE(peer->port.value == BASE + 1);
        } catch (const SocketException& e) {
            std::cerr << "  Unexpected: " << e.what() << "\n";
            threw = true;
        }
        srvThread.join();
        REQUIRE(!threw);
    }
}

// -----------------------------------------------------------------------
// Happy path: accept() returns unique_ptr<TcpSocket>
// -----------------------------------------------------------------------
static void test_happy_accept() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket::accept() returns unique_ptr<TcpSocket>");
    {
        TcpSocket srv;
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 2}));
        REQUIRE(srv.listen(1));

        std::thread clt([] {
            try {
                TcpSocket c;
                c.connect("127.0.0.1", Port{BASE + 2});
            } catch (...) {
            }
        });

        auto peer = srv.accept();
        REQUIRE(peer != nullptr);
        REQUIRE(peer->isValid());
        // Confirm we got an actual TcpSocket (not just a Socket).
        // The peer must be able to call TcpSocket-typed methods:
        auto ep = peer->getPeerEndpoint();
        REQUIRE(ep.has_value());

        clt.join();
    }
}

// -----------------------------------------------------------------------
// Happy path: send / receive
// -----------------------------------------------------------------------
static void test_happy_send_receive() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: send() / receive() exchange data");
    {
        const std::string msg = "hello-tcp-typed";
        std::string received;
        std::atomic<bool> ready{false};

        std::thread srvThread([&] {
            TcpSocket srv;
            srv.setReuseAddress(true);
            if (!srv.bind("127.0.0.1", Port{BASE + 3}) || !srv.listen(1)) {
                ready = true;
                return;
            }
            ready = true;
            auto peer = srv.accept();
            if (peer) {
                char buf[256] = {};
                int r = peer->receive(buf, sizeof(buf) - 1);
                if (r > 0) received.assign(buf, r);
            }
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        TcpSocket c;
        REQUIRE(c.connect("127.0.0.1", Port{BASE + 3}));
        int sent = c.send(msg.data(), msg.size());
        REQUIRE(sent == static_cast<int>(msg.size()));
        c.close();

        srvThread.join();
        REQUIRE(received == msg);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: sendAll() / receiveAll() exchange exact bytes");
    {
        const std::string msg = "exact-byte-transfer";
        std::string received(msg.size(), '\0');
        std::atomic<bool> ready{false};

        std::thread srvThread([&] {
            TcpSocket srv;
            srv.setReuseAddress(true);
            if (!srv.bind("127.0.0.1", Port{BASE + 4}) || !srv.listen(1)) {
                ready = true;
                return;
            }
            ready = true;
            auto peer = srv.accept();
            if (peer) {
                peer->receiveAll(received.data(), received.size());
            }
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        TcpSocket c;
        REQUIRE(c.connect("127.0.0.1", Port{BASE + 4}));
        REQUIRE(c.sendAll(msg.data(), msg.size()));
        c.close();

        srvThread.join();
        REQUIRE(received == msg);
    }
}

// -----------------------------------------------------------------------
// Happy path: sendAll with progress callback
// -----------------------------------------------------------------------
static void test_happy_progress_callback() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: sendAll() progress callback is invoked");
    {
        const std::string msg = "progress-data-test";
        std::atomic<bool> ready{false};
        size_t reportedSent = 0;
        size_t reportedTotal = 0;

        std::thread srvThread([&] {
            TcpSocket srv;
            srv.setReuseAddress(true);
            if (!srv.bind("127.0.0.1", Port{BASE + 5}) || !srv.listen(1)) {
                ready = true;
                return;
            }
            ready = true;
            auto peer = srv.accept();
            if (peer) {
                std::string buf(msg.size(), '\0');
                peer->receiveAll(buf.data(), buf.size());
            }
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        TcpSocket c;
        REQUIRE(c.connect("127.0.0.1", Port{BASE + 5}));
        bool ok
            = c.sendAll(msg.data(), msg.size(), [&](size_t sent, size_t total) {
                  reportedSent = sent;
                  reportedTotal = total;
              });
        REQUIRE(ok);
        REQUIRE(reportedSent == msg.size());
        REQUIRE(reportedTotal == msg.size());
        c.close();

        srvThread.join();
    }
}

// -----------------------------------------------------------------------
// Happy path: move semantics
// -----------------------------------------------------------------------
static void test_happy_move() {
    BEGIN_TEST("TcpSocket: move construction transfers ownership");
    {
        TcpSocket a;
        REQUIRE(a.isValid());
        TcpSocket b(std::move(a));
        REQUIRE(b.isValid());
        // a is in moved-from state: isValid() returns false
        REQUIRE(!a.isValid()); //-V1001
    }

    BEGIN_TEST("TcpSocket: move assignment transfers ownership");
    {
        TcpSocket a;
        TcpSocket b;
        REQUIRE(a.isValid());
        REQUIRE(b.isValid());
        b = std::move(a);
        REQUIRE(b.isValid());
        REQUIRE(!a.isValid()); //-V1001
    }
}

// -----------------------------------------------------------------------
// Happy path: socket options are accessible on TcpSocket
// -----------------------------------------------------------------------
static void test_happy_options() {
    BEGIN_TEST("TcpSocket: setNoDelay / setKeepAlive / setReuseAddress work");
    {
        TcpSocket s;
        REQUIRE(s.isValid());
        REQUIRE(s.setReuseAddress(true));
        REQUIRE(s.setNoDelay(true));
        REQUIRE(s.setKeepAlive(true));
        REQUIRE(s.setTimeout(std::chrono::seconds(10)));
        REQUIRE(s.setSendTimeout(std::chrono::seconds(10)));
        REQUIRE(s.setReceiveBufferSize(64 * 1024));
        REQUIRE(s.setSendBufferSize(64 * 1024));
    }

    BEGIN_TEST("TcpSocket: setBlocking / isBlocking round-trip");
    {
        TcpSocket s;
        REQUIRE(s.isBlocking()); // default is blocking
        REQUIRE(s.setBlocking(false));
        REQUIRE(!s.isBlocking());
        REQUIRE(s.setBlocking(true));
        REQUIRE(s.isBlocking());
    }
}

// -----------------------------------------------------------------------
// Sad path: construction failures throw SocketException
// -----------------------------------------------------------------------
static void test_sad_construction() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket(ServerBind): throws on port-in-use (no reuseAddr)");
    {
        // First socket holds the port
        TcpSocket first(AddressFamily::IPv4,
            ServerBind{"127.0.0.1", Port{BASE + 10}, 5, false});

        bool threw = false;
        SocketError code = SocketError::None;
        try {
            TcpSocket second(AddressFamily::IPv4,
                ServerBind{"127.0.0.1", Port{BASE + 10}, 5, false});
        } catch (const SocketException& e) {
            threw = true;
            code = e.errorCode();
        }
        REQUIRE(threw);
        REQUIRE(code == SocketError::BindFailed);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket(ServerBind): throws on invalid bind address");
    {
        bool threw = false;
        SocketError code = SocketError::None;
        try {
            TcpSocket s(AddressFamily::IPv4,
                ServerBind{"999.999.999.999", Port{BASE + 11}});
        } catch (const SocketException& e) {
            threw = true;
            code = e.errorCode();
        }
        REQUIRE(threw);
        REQUIRE(code == SocketError::BindFailed);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket(ConnectTo): throws when nothing is listening");
    {
        bool threw = false;
        SocketError code = SocketError::None;
        std::string whatStr;
        try {
            // Port 21899 is in our exclusive range but nothing is listening
            TcpSocket c(AddressFamily::IPv4,
                ConnectTo{
                    "127.0.0.1", Port{21899}, std::chrono::milliseconds{500}});
        } catch (const SocketException& e) {
            threw = true;
            code = e.errorCode();
            whatStr = e.what();
        }
        REQUIRE(threw);
        REQUIRE((code == SocketError::ConnectFailed
            || code == SocketError::Timeout));
        REQUIRE(!whatStr.empty());
    }
}

// -----------------------------------------------------------------------
// Sad path: post-construction failures via manual calls
// -----------------------------------------------------------------------
static void test_sad_operations() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: bind() fails on bad address (manual call)");
    {
        TcpSocket s;
        bool ok = s.bind("999.999.999.999", Port{BASE + 20});
        REQUIRE(!ok);
        REQUIRE(s.getLastError() == SocketError::BindFailed);
    }

    BEGIN_TEST("TcpSocket: connect() fails when nothing is listening");
    {
        TcpSocket s;
        bool ok = s.connect(
            "127.0.0.1", Port{21898}, std::chrono::milliseconds{500});
        REQUIRE(!ok);
        REQUIRE((s.getLastError() == SocketError::ConnectFailed
            || s.getLastError() == SocketError::Timeout));
    }

    BEGIN_TEST("TcpSocket: send() on closed socket returns error");
    {
        TcpSocket s;
        s.close();
        REQUIRE(!s.isValid());
        int r = s.send("x", 1);
        REQUIRE(r < 0);
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("TcpSocket: receive() on closed socket returns error");
    {
        TcpSocket s;
        s.close();
        char buf[16];
        int r = s.receive(buf, sizeof(buf));
        REQUIRE(r < 0);
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("TcpSocket: accept() returns nullptr on non-listening socket");
    {
        TcpSocket s;
        // Not listening — accept should fail and return nullptr
        auto peer = s.accept();
        REQUIRE(peer == nullptr);
        REQUIRE(s.getLastError() == SocketError::AcceptFailed);
    }

    BEGIN_TEST("TcpSocket: sendAll() on closed socket returns false");
    {
        TcpSocket s;
        s.close();
        bool ok = s.sendAll("data", 4);
        REQUIRE(!ok);
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("TcpSocket: receiveAll() on closed socket returns false");
    {
        TcpSocket s;
        s.close();
        char buf[8] = {};
        bool ok = s.receiveAll(buf, sizeof(buf));
        REQUIRE(!ok);
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("TcpSocket: listen() without bind fails gracefully");
    {
        TcpSocket s;
        // listen() on unbound socket is OS-dependent but should not crash
        bool ok = s.listen(1);
        // We only require it doesn't crash; some OSes let loopback-assign
        // succeed, others fail.
        (void)ok;
        REQUIRE(s.isValid()); // fd still valid even if listen failed
    }
}

// -----------------------------------------------------------------------
// Sad path: non-blocking connect returns WouldBlock immediately
// -----------------------------------------------------------------------
static void test_sad_timeout() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: connect with Milliseconds{0} on non-blocking socket "
               "returns WouldBlock");
    {
        // Milliseconds{0} = non-blocking connect: returns immediately.
        // With nothing listening, the OS will either:
        //   a) refuse immediately (ECONNREFUSED → ConnectFailed), or
        //   b) return WouldBlock (still in progress after SYN sent).
        // Both are valid outcomes for a non-blocking connect attempt.
        TcpSocket s;
        REQUIRE(s.setBlocking(false));
        bool ok
            = s.connect("127.0.0.1", Port{21899}, std::chrono::milliseconds{0});
        // Non-blocking to nothing → should not succeed
        REQUIRE(!ok);
        REQUIRE((s.getLastError() == SocketError::WouldBlock
            || s.getLastError() == SocketError::ConnectFailed));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: connect() with short timeout to refused port");
    {
        // Port 21898 has no listener — ECONNREFUSED arrives quickly.
        TcpSocket s;
        bool ok = s.connect(
            "127.0.0.1", Port{21898}, std::chrono::milliseconds{2000});
        REQUIRE(!ok);
        REQUIRE((s.getLastError() == SocketError::ConnectFailed
            || s.getLastError() == SocketError::Timeout));
    }
}

// -----------------------------------------------------------------------
// Happy path: endpoint queries
// -----------------------------------------------------------------------
static void test_happy_endpoints() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: getLocalEndpoint() reflects bind address");
    {
        TcpSocket s;
        REQUIRE(s.bind("127.0.0.1", Port{BASE + 30}));
        auto ep = s.getLocalEndpoint();
        REQUIRE(ep.has_value());
        REQUIRE(ep->address == "127.0.0.1");
        REQUIRE(ep->port.value == BASE + 30);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: getPeerEndpoint() populated after connect");
    {
        std::atomic<bool> ready{false};
        std::thread srvThread([&] {
            TcpSocket srv;
            srv.setReuseAddress(true);
            if (!srv.bind("127.0.0.1", Port{BASE + 31}) || !srv.listen(1)) {
                ready = true;
                return;
            }
            ready = true;
            srv.accept(); // accept and discard
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        TcpSocket c;
        REQUIRE(c.connect("127.0.0.1", Port{BASE + 31}));
        auto peer = c.getPeerEndpoint();
        REQUIRE(peer.has_value());
        REQUIRE(peer->port.value == BASE + 31);

        srvThread.join();
    }
}

// -----------------------------------------------------------------------
// Happy path: close + isValid lifecycle
// -----------------------------------------------------------------------
static void test_happy_lifecycle() {
    BEGIN_TEST(
        "TcpSocket: isValid() true after construction, false after close");
    {
        TcpSocket s;
        REQUIRE(s.isValid());
        s.close();
        REQUIRE(!s.isValid());
        // Double-close must not crash
        s.close();
        REQUIRE(!s.isValid());
    }
}

int main() {
    std::cout << "=== TcpSocket: Happy and Sad Path Tests ===\n\n";

    test_happy_construction();
    test_happy_accept();
    test_happy_send_receive();
    test_happy_progress_callback();
    test_happy_move();
    test_happy_options();
    test_sad_construction();
    test_sad_operations();
    test_sad_timeout();
    test_happy_endpoints();
    test_happy_lifecycle();

    return test_summary();
}
