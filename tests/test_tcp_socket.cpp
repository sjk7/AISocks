// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

//
// test_tcp_socket.cpp  Happy and sad path tests for TcpSocket specifically.
//
// These tests verify the TcpSocket-typed API: that correct operations work,
// that invalid operations produce the expected errors, and that the type-safe
// design contracts hold (accept returns unique_ptr<TcpSocket>, not Socket).

#include "TcpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using namespace aiSocks;

// -----------------------------------------------------------------------
// Happy path: construction
// -----------------------------------------------------------------------
static void test_happy_construction() {
    BEGIN_TEST("TcpSocket: default ctor (TCP/IPv4) is valid");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv4);
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("TcpSocket: explicit IPv6 ctor is valid");
    {
        auto s = TcpSocket::createRaw(AddressFamily::IPv6);
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv6);
    }

    BEGIN_TEST("TcpSocket: ServerBind ctor binds and listens in one step");
    {
        TcpSocket srv(AddressFamily::IPv4, ServerBind{"127.0.0.1", Port::any});
        REQUIRE(srv.isValid());
        auto ep = srv.getLocalEndpoint();
        REQUIRE(ep.isSuccess());
        REQUIRE(ep.value().port.value() != 0);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: ConnectArgs ctor creates a connected socket");
    {
        std::atomic<bool> ready{false};
        std::atomic<uint16_t> srvPort{0};
        std::thread srvThread([&] {
            TcpSocket srv(
                AddressFamily::IPv4, ServerBind{"127.0.0.1", Port::any});
            REQUIRE(srv.isValid());
            auto ep = srv.getLocalEndpoint();
            srvPort.store(ep.isSuccess() ? ep.value().port.value() : 0);
            ready = true;
            auto peer = srv.accept();
            if (peer == nullptr) {
                // Wait and retry once
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                peer = srv.accept();
            }
            REQUIRE(peer != nullptr);
            REQUIRE(peer->isValid());
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        TcpSocket c(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{srvPort.load()}});
        REQUIRE(c.isValid());

        srvThread.join();
    }
}

// -----------------------------------------------------------------------
// Happy path: accept() returns unique_ptr<TcpSocket>
// -----------------------------------------------------------------------
static void test_happy_accept() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket::accept() returns unique_ptr<TcpSocket>");
    {
        auto srv = TcpSocket::createRaw();
        REQUIRE(srv.setReuseAddress(true));
        REQUIRE(srv.bind("127.0.0.1", Port::any));
        REQUIRE(srv.listen(1));
        uint16_t srvPort = 0;
        {
            auto ep = srv.getLocalEndpoint();
            srvPort = ep.isSuccess() ? ep.value().port.value() : 0;
        }
        REQUIRE(srvPort != 0);

        std::thread clt([srvPort] {
            auto c = TcpSocket::createRaw();
            (void)c.connect("127.0.0.1", Port{srvPort});
        });

        auto peer = srv.accept();
        REQUIRE(peer != nullptr);
        REQUIRE(peer->isValid());
        // Confirm we got an actual TcpSocket (not just a Socket).
        // The peer must be able to call TcpSocket-typed methods:
        auto ep = peer->getPeerEndpoint();
        REQUIRE(ep.isSuccess());

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
        std::atomic<uint16_t> srvPort{0};

        std::thread srvThread([&] {
            auto srv = TcpSocket::createRaw();
            REQUIRE(srv.setReuseAddress(true));
            if (!srv.bind("127.0.0.1", Port::any) || !srv.listen(1)) {
                ready = true;
                return;
            }
            auto ep = srv.getLocalEndpoint();
            srvPort.store(ep.isSuccess() ? ep.value().port.value() : 0);
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

        auto c = TcpSocket::createRaw();
        REQUIRE(c.connect("127.0.0.1", Port{srvPort.load()}));
        int sent = c.send(msg.data(), msg.size());
        REQUIRE(sent == static_cast<int>(msg.size()));
        c.close();

        srvThread.join();
        REQUIRE(received == msg); //-V547
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: sendAll() / receiveAll() exchange exact bytes");
    {
        const std::string msg = "exact-byte-transfer";
        std::string received(msg.size(), '\0');
        std::atomic<bool> ready{false};
        std::atomic<uint16_t> srvPort{0};

        std::thread srvThread([&] {
            auto srv = TcpSocket::createRaw();
            REQUIRE(srv.setReuseAddress(true));
            if (!srv.bind("127.0.0.1", Port::any) || !srv.listen(1)) {
                ready = true;
                return;
            }
            auto ep = srv.getLocalEndpoint();
            srvPort.store(ep.isSuccess() ? ep.value().port.value() : 0);
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

        auto c = TcpSocket::createRaw();
        REQUIRE(c.connect("127.0.0.1", Port{srvPort.load()}));
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
        std::atomic<uint16_t> srvPort{0};
        size_t reportedSent = 0;
        size_t reportedTotal = 0;

        std::thread srvThread([&] {
            auto srv = TcpSocket::createRaw();
            REQUIRE(srv.setReuseAddress(true));
            if (!srv.bind("127.0.0.1", Port::any) || !srv.listen(1)) {
                ready = true;
                return;
            }
            auto ep = srv.getLocalEndpoint();
            srvPort.store(ep.isSuccess() ? ep.value().port.value() : 0);
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

        auto c = TcpSocket::createRaw();
        REQUIRE(c.connect("127.0.0.1", Port{srvPort.load()}));
        bool ok
            = c.sendAll(msg.data(), msg.size(), [&](size_t sent, size_t total) {
                  reportedSent = sent;
                  reportedTotal = total;
                  return 0; // continue
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
        auto a = TcpSocket::createRaw();
        REQUIRE(a.isValid());
        TcpSocket b(std::move(a));
        REQUIRE(b.isValid());
        // a is in moved-from state: isValid() returns false
        REQUIRE(!a.isValid()); //-V1001
    }

    BEGIN_TEST("TcpSocket: move assignment transfers ownership");
    {
        auto a = TcpSocket::createRaw();
        auto b = TcpSocket::createRaw();
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
        auto s = TcpSocket::createRaw();
        REQUIRE(s.isValid());
        REQUIRE(s.setReuseAddress(true));
        REQUIRE(s.setNoDelay(true));
        REQUIRE(s.setKeepAlive(true));
        REQUIRE(s.setReceiveTimeout(std::chrono::seconds(10)));
        REQUIRE(s.setSendTimeout(std::chrono::seconds(10)));
        REQUIRE(s.setReceiveBufferSize(64 * 1024));
        REQUIRE(s.setSendBufferSize(64 * 1024));
    }

    BEGIN_TEST("TcpSocket: setBlocking / isBlocking round-trip");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.isBlocking()); // default is blocking
        REQUIRE(s.setBlocking(false));
        REQUIRE(!s.isBlocking());
        REQUIRE(s.setBlocking(true));
        REQUIRE(s.isBlocking());
    }
}

// -----------------------------------------------------------------------
// Sad path: construction failures leave socket invalid
// -----------------------------------------------------------------------
static void test_sad_construction() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: ServerBind fails on port in use");
    {
        // First server should succeed
        TcpSocket first(AddressFamily::IPv4,
            ServerBind{"127.0.0.1", Port::any, Backlog{5}, false});
        REQUIRE(first.isValid());
        uint16_t firstPort = 0;
        {
            auto ep = first.getLocalEndpoint();
            firstPort = ep.isSuccess() ? ep.value().port.value() : 0;
        }
        REQUIRE(firstPort != 0);

        // Second server should fail
        auto result = SocketFactory::createTcpServer(AddressFamily::IPv4,
            ServerBind{"127.0.0.1", Port{firstPort}, Backlog{5}, false});
        REQUIRE(result.isError());
        REQUIRE(result.error() != SocketError::None);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket(ServerBind): fails on invalid bind address");
    {
        auto result = SocketFactory::createTcpServer(
            AddressFamily::IPv4, ServerBind{"999.999.999.999", Port::any});
        REQUIRE(result.isError());
        REQUIRE(result.error() == SocketError::BindFailed);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket(ConnectArgs): fails when nothing is listening");
    {
        // Port 21899 is in our exclusive range but nothing is listening
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{21899}, Milliseconds{100}});
        REQUIRE(result.isError());
        REQUIRE(result.error() == SocketError::ConnectFailed
            || result.error() == SocketError::ConnectionReset
            || result.error() == SocketError::Timeout);
    }
}

// -----------------------------------------------------------------------
// Sad path: post-construction failures via manual calls
// -----------------------------------------------------------------------
static void test_sad_operations() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: bind() fails on bad address (manual call)");
    {
        auto s = TcpSocket::createRaw();
        bool ok = s.bind("999.999.999.999", Port::any);
        REQUIRE(!ok);
        REQUIRE(s.getLastError() == SocketError::BindFailed);
    }

    BEGIN_TEST("TcpSocket: connect() fails when nothing is listening");
    {
        auto s = TcpSocket::createRaw();
        bool ok = s.connect(
            "127.0.0.1", Port{21898}, std::chrono::milliseconds{100});
        REQUIRE(!ok);
        REQUIRE((s.getLastError() == SocketError::ConnectFailed
            || s.getLastError() == SocketError::Timeout));
    }

    BEGIN_TEST("TcpSocket: send() on closed socket returns error");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        REQUIRE(!s.isValid());
        int r = s.send("x", 1);
        REQUIRE(r < 0);
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("TcpSocket: receive() on closed socket returns error");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        char buf[16];
        int r = s.receive(buf, sizeof(buf));
        REQUIRE(r < 0);
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("TcpSocket: accept() returns nullptr on non-listening socket");
    {
        auto s = TcpSocket::createRaw();
        // Not listening  accept should fail and return nullptr
        auto peer = s.accept();
        REQUIRE(peer == nullptr);
        REQUIRE(s.getLastError() == SocketError::AcceptFailed);
    }

    BEGIN_TEST("TcpSocket: sendAll() on closed socket returns false");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        bool ok = s.sendAll("data", 4);
        REQUIRE(!ok);
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("TcpSocket: receiveAll() on closed socket returns false");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        char buf[8] = {};
        bool ok = s.receiveAll(buf, sizeof(buf));
        REQUIRE(!ok);
        REQUIRE(s.getLastError() == SocketError::InvalidSocket);
    }

    BEGIN_TEST("TcpSocket: listen() without bind fails gracefully");
    {
        auto s = TcpSocket::createRaw();
        // listen() on unbound socket is OS-dependent but should not crash
        bool ok = s.listen(1);
        // If listen() fails, the socket should be invalidated
        if (!ok) {
            REQUIRE(
                !s.isValid()); // socket should be invalid after listen failure
            REQUIRE(s.getLastError() == SocketError::ListenFailed);
        } else {
            // If listen() succeeded (some OSes auto-bind), socket should be
            // valid
            REQUIRE(s.isValid());
        }
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
        //   a) refuse immediately (ECONNREFUSED  ConnectFailed), or
        //   b) return WouldBlock (still in progress after SYN sent).
        // Both are valid outcomes for a non-blocking connect attempt.
        auto s = TcpSocket::createRaw();
        REQUIRE(s.setBlocking(false));
        bool ok
            = s.connect("127.0.0.1", Port{21899}, std::chrono::milliseconds{0});
        // Non-blocking to nothing  should not succeed
        REQUIRE(!ok);
        REQUIRE((s.getLastError() == SocketError::WouldBlock
            || s.getLastError() == SocketError::ConnectFailed));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: connect() with short timeout to refused port");
    {
        // Port 21898 has no listener  ECONNREFUSED arrives quickly.
        auto s = TcpSocket::createRaw();
        bool ok = s.connect(
            "127.0.0.1", Port{21898}, std::chrono::milliseconds{100});
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
        auto s = TcpSocket::createRaw();
        REQUIRE(s.bind("127.0.0.1", Port::any));
        auto ep = s.getLocalEndpoint();
        REQUIRE(ep.isSuccess());
        REQUIRE(ep.value().address == "127.0.0.1");
        REQUIRE(ep.value().port.value() != 0);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("TcpSocket: getPeerEndpoint() populated after connect");
    {
        std::atomic<bool> ready{false};
        std::atomic<uint16_t> srvPort31{0};
        std::thread srvThread([&] {
            auto srv = TcpSocket::createRaw();
            REQUIRE(srv.setReuseAddress(true));
            if (!srv.bind("127.0.0.1", Port::any) || !srv.listen(1)) {
                ready = true;
                return;
            }
            auto ep = srv.getLocalEndpoint();
            srvPort31.store(ep.isSuccess() ? ep.value().port.value() : 0);
            ready = true;
            (void)srv.accept(); // accept and discard
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        auto c = TcpSocket::createRaw();
        REQUIRE(c.connect("127.0.0.1", Port{srvPort31.load()}));
        auto peer = c.getPeerEndpoint();
        REQUIRE(peer.isSuccess());
        REQUIRE(peer.value().port.value() == srvPort31.load());

        srvThread.join();
    }
}

// -----------------------------------------------------------------------
// Happy path: sendChunked
// -----------------------------------------------------------------------
static void test_happy_send_chunked() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST(
        "TcpSocket: sendChunked() delivers a multi-chunk payload (200 KB)");
    {
        // 200 KB forces at least 3 iterations of the 64 KB inner chunk loop.
        constexpr size_t payloadSize = 200 * 1024;
        std::string payload(payloadSize, '\0');
        for (size_t i = 0; i < payloadSize; ++i)
            payload[i] = static_cast<char>(i & 0xFF);

        std::string received;
        received.reserve(payloadSize);
        std::atomic<bool> ready{false};
        std::atomic<uint16_t> port{0};

        std::thread srvThread([&] {
            auto srv = TcpSocket::createRaw();
            REQUIRE(srv.setReuseAddress(true));
            if (!srv.bind("127.0.0.1", Port::any) || !srv.listen(1)) {
                ready = true;
                return;
            }
            auto ep = srv.getLocalEndpoint();
            port.store(ep.isSuccess() ? ep.value().port.value() : 0);
            ready = true;
            auto peer = srv.accept(); // blocking: waits for client
            if (peer) {
                char buf[8192];
                while (received.size() < payloadSize) {
                    int n = peer->receive(buf, sizeof(buf));
                    if (n > 0)
                        received.append(buf, static_cast<size_t>(n));
                    else
                        break; // EOF or error
                }
            }
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        REQUIRE(port.load() != 0);
        auto c = TcpSocket::createRaw();
        REQUIRE(c.connect("127.0.0.1", Port{port.load()}));

        // sendChunked on a blocking socket drives the full chunk loop.
        // On loopback this completes in one call, but the loop inside
        // sendChunked still iterates through each 64 KB slice.
        size_t totalSent = 0;
        while (totalSent < payloadSize) {
            int n = c.sendChunked(
                payload.data() + totalSent, payloadSize - totalSent);
            REQUIRE(n > 0);
            if (n <= 0) break;
            totalSent += static_cast<size_t>(n);
        }
        REQUIRE(totalSent == payloadSize);
        c.close();

        srvThread.join();
        REQUIRE(received.size() == payloadSize);
        REQUIRE(received == payload);
    }
}

// -----------------------------------------------------------------------
// Happy path: close + isValid lifecycle
// -----------------------------------------------------------------------
static void test_happy_lifecycle() {
    BEGIN_TEST(
        "TcpSocket: isValid() true after construction, false after close");
    {
        auto s = TcpSocket::createRaw();
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

    using clock = std::chrono::steady_clock;
    auto time = [&](const char* name, auto fn) {
        auto t0 = clock::now();
        fn();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - t0)
                      .count();
        std::cout << "  [timing] " << name << ": " << ms << " ms\n";
    };

    time("test_happy_construction", test_happy_construction);
    time("test_happy_accept", test_happy_accept);
    time("test_happy_send_receive", test_happy_send_receive);
    time("test_happy_progress_callback", test_happy_progress_callback);
    time("test_happy_move", test_happy_move);
    time("test_happy_options", test_happy_options);
    time("test_sad_construction", test_sad_construction);
    time("test_sad_operations", test_sad_operations);
    time("test_sad_timeout", test_sad_timeout);
    time("test_happy_endpoints", test_happy_endpoints);
    time("test_happy_send_chunked", test_happy_send_chunked);
    time("test_happy_lifecycle", test_happy_lifecycle);

    return test_summary();
}
