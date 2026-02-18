// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
//
// Tests for features added in the second pass:
//   Endpoint / getLocalEndpoint / getPeerEndpoint
//   setSendTimeout
//   setNoDelay (TCP_NODELAY)
//   UDP sendTo / receiveFrom
//   shutdown(ShutdownHow)
//   setKeepAlive (SO_KEEPALIVE)

#include "TcpSocket.h"
#include "UdpSocket.h"
#include "test_helpers.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace aiSocks;

// Port block: 20000 – 20099 (no overlap with existing test suites)
static constexpr int BASE = 20000;

// -----------------------------------------------------------------------
// 1. Endpoint / getLocalEndpoint / getPeerEndpoint
// -----------------------------------------------------------------------
static void test_endpoints() {
    BEGIN_TEST("getLocalEndpoint: address and port correct after bind");
    {
        auto s = TcpSocket::createRaw();
        s.setReuseAddress(true);
        REQUIRE(s.bind("127.0.0.1", Port{BASE}));
        auto ep = s.getLocalEndpoint();
        REQUIRE(ep.has_value());
        if (!ep) return;
        const auto& e = *ep;
        REQUIRE(e.port == Port{BASE} && e.address == "127.0.0.1"
            && e.family == AddressFamily::IPv4);
    }

    BEGIN_TEST(
        "getLocalEndpoint: ephemeral port non-zero after bind on port 0");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.bind("127.0.0.1", Port{0}));
        auto ep = s.getLocalEndpoint();
        REQUIRE(ep.has_value());
        if (!ep) return;
        const auto& e = *ep;
        REQUIRE(e.port.value != 0);
        std::cout << "  assigned ephemeral port: " << e.port.value << "\n";
    }

    BEGIN_TEST("getPeerEndpoint: populated after TCP connect");
    {
        auto srv = TcpSocket::createRaw();
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 1}));
        REQUIRE(srv.listen(1));

        std::thread t([&]() {
            auto peer = srv.accept();
            // peer goes out of scope; connection closes
        });

        auto c = TcpSocket::createRaw();
        REQUIRE(c.connect("127.0.0.1", Port{BASE + 1}));
        auto ep = c.getPeerEndpoint();
        REQUIRE(ep.has_value());
        if (!ep) {
            t.join();
            return;
        }
        const auto& e = *ep;
        REQUIRE(e.port == Port{BASE + 1} && e.address == "127.0.0.1");
        t.join();
    }

    BEGIN_TEST("getLocalEndpoint: nullopt on closed socket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        auto ep = s.getLocalEndpoint();
        REQUIRE(!ep.has_value());
    }

    BEGIN_TEST("getPeerEndpoint: nullopt on unconnected socket");
    {
        auto s = TcpSocket::createRaw();
        (void)s.bind("127.0.0.1", Port{0});
        auto ep = s.getPeerEndpoint();
        REQUIRE(!ep.has_value());
    }

    BEGIN_TEST("Endpoint::toString: returns addr:port string");
    {
        Endpoint ep{"192.168.1.1", Port{8080}, AddressFamily::IPv4};
        REQUIRE(ep.toString() == "192.168.1.1:8080");
    }
}

// -----------------------------------------------------------------------
// 2. setSendTimeout
// -----------------------------------------------------------------------
static void test_send_timeout() {
    BEGIN_TEST("setSendTimeout: succeeds with positive duration");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.setSendTimeout(Milliseconds{5000}));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setSendTimeout: Milliseconds{0} disables timeout");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.setSendTimeout(Milliseconds{0}));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setSendTimeout: fails on closed socket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        REQUIRE(!s.setSendTimeout(Milliseconds{1000}));
    }
}

// -----------------------------------------------------------------------
// 3. setNoDelay
// -----------------------------------------------------------------------
static void test_no_delay() {
    BEGIN_TEST("setNoDelay(true): enables TCP_NODELAY");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.setNoDelay(true));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setNoDelay: can be toggled off");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.setNoDelay(true));
        REQUIRE(s.setNoDelay(false));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setNoDelay: fails on closed socket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        REQUIRE(!s.setNoDelay(true));
    }
}

// -----------------------------------------------------------------------
// 4. UDP sendTo / receiveFrom
// -----------------------------------------------------------------------
static void test_udp() {
    BEGIN_TEST("UDP sendTo/receiveFrom: basic loopback datagram exchange");
    {
        UdpSocket receiver;
        receiver.setReuseAddress(true);
        REQUIRE(receiver.bind("127.0.0.1", Port{BASE + 10}));
        receiver.setReceiveTimeout(Milliseconds{2000});

        UdpSocket sender;

        const char msg[] = "hello udp";
        Endpoint dest{"127.0.0.1", Port{BASE + 10}, AddressFamily::IPv4};
        int sent = sender.sendTo(msg, sizeof(msg) - 1, dest);
        REQUIRE(sent == static_cast<int>(sizeof(msg) - 1));

        char buf[64] = {};
        Endpoint from;
        int recvd = receiver.receiveFrom(buf, sizeof(buf), from);
        REQUIRE(recvd == static_cast<int>(sizeof(msg) - 1));
        REQUIRE(std::string(buf, static_cast<size_t>(recvd)) == "hello udp");
        REQUIRE(from.port.value != 0);
        REQUIRE(from.address == "127.0.0.1");
        std::cout << "  sender seen as: " << from.toString() << "\n";
    }

    BEGIN_TEST("UDP sendTo/receiveFrom: multiple datagrams in sequence");
    {
        UdpSocket srv;
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 11}));
        srv.setReceiveTimeout(Milliseconds{2000});

        UdpSocket cli;
        Endpoint dest{"127.0.0.1", Port{BASE + 11}, AddressFamily::IPv4};

        for (int i = 0; i < 3; ++i) {
            std::string payload = "pkt" + std::to_string(i);
            int s = cli.sendTo(payload.data(), payload.size(), dest);
            REQUIRE(s == static_cast<int>(payload.size()));

            char buf[64] = {};
            Endpoint from;
            int r = srv.receiveFrom(buf, sizeof(buf), from);
            REQUIRE(r == static_cast<int>(payload.size()));
            REQUIRE(std::string(buf, static_cast<size_t>(r)) == payload);
        }
    }
}

// -----------------------------------------------------------------------
// 4b. Connected-mode UDP (connect() + send/receive)
// -----------------------------------------------------------------------
// UDP sockets support connect() to record a default peer address in the
// kernel.  After connect(), send() / receive() work without per-call
// endpoint parameters, and only datagrams from the connected peer are
// delivered to receive().  This is sometimes called "connected UDP".
static void test_udp_connected() {
    BEGIN_TEST("connected-mode UDP: connect() then send() / receive()");
    {
        UdpSocket server;
        server.setReuseAddress(true);
        REQUIRE(server.bind("127.0.0.1", Port{BASE + 30}));
        server.setReceiveTimeout(Milliseconds{2000});

        UdpSocket client;
        // Connect the client to the server so send()/receive() need no
        // per-call endpoint.
        REQUIRE(client.connect("127.0.0.1", Port{BASE + 30}));

        // After connect(), getPeerEndpoint() is populated on UDP too.
        auto peer = client.getPeerEndpoint();
        REQUIRE(peer.has_value() && peer->port == Port{BASE + 30});

        // Send via the connected path (no Endpoint argument).
        const char msg[] = "connected-udp";
        int sent = client.send(msg, sizeof(msg) - 1);
        REQUIRE(sent == static_cast<int>(sizeof(msg) - 1));

        // Server receives it via recvfrom so we can inspect the origin.
        char buf[64] = {};
        Endpoint from;
        int recvd = server.receiveFrom(buf, sizeof(buf), from);
        REQUIRE(recvd == static_cast<int>(sizeof(msg) - 1));
        REQUIRE(
            std::string(buf, static_cast<size_t>(recvd)) == "connected-udp");
        REQUIRE(from.address == "127.0.0.1");
        std::cout << "  datagram arrived from: " << from.toString() << "\n";
    }
}

// -----------------------------------------------------------------------
// 4c. UDP transfer tests — payload integrity, bidirectional echo,
//     max-size datagrams.
// -----------------------------------------------------------------------
// Notable UDP constraints exercised here:
//   • UDP is message-oriented: each send() produces exactly one recvfrom()
//     with the same byte count.
//   • A datagram that is too large for the receiver's buffer is silently
//     truncated (POSIX) or returns WSAEMSGSIZE (Windows); we stay well
//     under the safe loopback MTU (~65507 bytes) with 8192-byte payloads.
//   • Datagram order is not guaranteed in general but IS preserved on
//     loopback — we rely on this for the sequenced echo test.
static void test_udp_transfer() {
    BEGIN_TEST("UDP transfer: 20 datagrams, sequential payload integrity");
    {
        UdpSocket srv;
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 50}));
        srv.setReceiveTimeout(Milliseconds{2000});

        UdpSocket cli;
        Endpoint dest{"127.0.0.1", Port{BASE + 50}, AddressFamily::IPv4};

        for (int i = 0; i < 20; ++i) {
            // Build a payload with a recognisable sequence number embedded.
            char payload[64];
            int payloadLen
                = std::snprintf(payload, sizeof(payload), "datagram-%03d", i);
            REQUIRE(payloadLen > 0);

            int s = cli.sendTo(payload, static_cast<size_t>(payloadLen), dest);
            REQUIRE(s == payloadLen);

            char buf[128] = {};
            Endpoint from;
            int r = srv.receiveFrom(buf, sizeof(buf), from);
            REQUIRE(r == payloadLen);
            REQUIRE(std::string(buf, static_cast<size_t>(r))
                == std::string(payload, static_cast<size_t>(payloadLen)));
        }
    }

    BEGIN_TEST(
        "UDP transfer: bidirectional echo (client sends, server echoes)");
    {
        UdpSocket srv;
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 51}));
        srv.setReceiveTimeout(Milliseconds{2000});

        UdpSocket cli;
        cli.setReceiveTimeout(Milliseconds{2000});
        Endpoint srvAddr{"127.0.0.1", Port{BASE + 51}, AddressFamily::IPv4};

        for (int i = 0; i < 5; ++i) {
            std::string out = "echo-" + std::to_string(i);

            // Client → server
            REQUIRE(cli.sendTo(out.data(), out.size(), srvAddr)
                == static_cast<int>(out.size()));

            // Server echoes back to origin
            char recvBuf[64] = {};
            Endpoint from;
            int r = srv.receiveFrom(recvBuf, sizeof(recvBuf), from);
            REQUIRE(r == static_cast<int>(out.size()));
            srv.sendTo(recvBuf, static_cast<size_t>(r), from);

            // Client receives echo
            char echoBuf[64] = {};
            Endpoint ignored;
            int er = cli.receiveFrom(echoBuf, sizeof(echoBuf), ignored);
            REQUIRE(er == static_cast<int>(out.size()));
            REQUIRE(std::string(echoBuf, static_cast<size_t>(er)) == out);
        }
    }

    BEGIN_TEST("UDP transfer: 8192-byte datagram round-trip");
    {
        UdpSocket srv;
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 52}));
        srv.setReceiveTimeout(Milliseconds{2000});

        UdpSocket cli;
        Endpoint dest{"127.0.0.1", Port{BASE + 52}, AddressFamily::IPv4};

        // Fill with a recognisable pattern.
        constexpr size_t SZ = 8192;
        std::vector<char> out(SZ);
        for (size_t i = 0; i < SZ; ++i) out[i] = static_cast<char>(i & 0xFF);

        REQUIRE(cli.sendTo(out.data(), SZ, dest) == static_cast<int>(SZ));

        std::vector<char> in(SZ + 1, 0);
        Endpoint from;
        int r = srv.receiveFrom(in.data(), in.size(), from);
        REQUIRE(r == static_cast<int>(SZ));
        REQUIRE(std::memcmp(out.data(), in.data(), SZ) == 0);
    }
}

// -----------------------------------------------------------------------
// 4d. Bulk throughput benchmarks — UDP vs TCP loopback
//
// These are not pass/fail correctness tests: they report bytes transferred
// and wall-clock throughput so the two protocols can be compared.
//
// Methodology:
//   • UDP: sender thread blasts N max-size datagrams (65507 B) freely;
//     receiver thread counts bytes independently — no per-datagram
//     rendezvous.  Measures raw kernel UDP loopback dispatch rate.
//   • TCP: sendAll / receiveAll over a thread-pair; measures sustained
//     streaming throughput including kernel TCP buffering.
// -----------------------------------------------------------------------
static void test_bulk_throughput() {
    // ---- UDP --------------------------------------------------------
    BEGIN_TEST(
        "UDP bulk throughput (loopback, 65507-byte datagrams, threaded)");
    {
        // Max safe UDP payload on loopback (65535 - 20 IP - 8 UDP header).
        // Keep COUNT modest so the test passes on slow/constrained CI runners.
        constexpr size_t DGRAM = 65507;
        constexpr int COUNT = 200; // 200 × 65507 B ≈ 12.5 MB
        constexpr size_t TOTAL = static_cast<size_t>(COUNT) * DGRAM;

        UdpSocket srv;
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 60}));
        // Ask the kernel for a large receive buffer (actual grant may differ).
        srv.setReceiveBufferSize(8 * 1024 * 1024);
        // Short timeout: receiver exits quickly once sender is done.
        srv.setReceiveTimeout(Milliseconds{200});

        std::atomic<size_t> recvTotal{0};
        std::atomic<size_t> recvCount{0};
        std::atomic<bool> senderDone{false};

        std::vector<char> recvBuf(DGRAM);

        // Receiver thread: drain until sender signals done then socket quiet.
        std::thread recvThread([&] {
            Endpoint from;
            while (true) {
                int r = srv.receiveFrom(recvBuf.data(), recvBuf.size(), from);
                if (r > 0) {
                    recvTotal += static_cast<size_t>(r);
                    ++recvCount;
                } else {
                    if (senderDone.load()) break; // sender done, go quiet
                }
            }
        });

        UdpSocket cli;
        cli.setSendBufferSize(8 * 1024 * 1024);
        Endpoint dest{"127.0.0.1", Port{BASE + 60}, AddressFamily::IPv4};
        std::vector<char> pkt(DGRAM, 0xAB);

        auto t0 = std::chrono::steady_clock::now();

        for (int i = 0; i < COUNT; ++i) cli.sendTo(pkt.data(), DGRAM, dest);

        auto t1 = std::chrono::steady_clock::now(); // time sender only
        senderDone = true;
        recvThread.join();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        size_t dropped = static_cast<size_t>(COUNT) - recvCount.load();
        double mbRecv = recvTotal.load() / (1024.0 * 1024.0);
        double mbSend = static_cast<double>(TOTAL) / (1024.0 * 1024.0);
        double mbps = mbSend / (ms / 1000.0); // throughput = sent / sender time

        std::cout << std::fixed << std::setprecision(1);
        std::cout << "  datagrams: " << COUNT << "  size: " << DGRAM
                  << " B  sent: " << mbSend << " MB  received: " << mbRecv
                  << " MB  dropped: " << dropped << "\n";
        std::cout << "  sender time: " << ms << " ms  send throughput: " << mbps
                  << " MB/s\n";

        if (dropped > 0)
            std::cout << "  NOTE: " << dropped
                      << " datagrams dropped (kernel buffer overflow)\n";
        REQUIRE(recvTotal.load() > 0);
    }

    // ---- TCP --------------------------------------------------------
    BEGIN_TEST("TCP bulk throughput (loopback, sendAll/receiveAll)");
    {
        constexpr size_t CHUNK = 64 * 1024; // 64 KB chunks
        constexpr size_t TOTAL = 4 * 1024
            * 1024; // 4 MB — enough for a throughput reading, fast on CI

        std::vector<char> sendBuf(CHUNK, 0xCD);
        std::vector<char> recvBuf(CHUNK);
        std::atomic<size_t> recvTotal{0};
        std::atomic<bool> ready{false};

        std::thread srvThread([&] {
            auto srv = TcpSocket::createRaw();
            srv.setReuseAddress(true);
            if (!srv.bind("127.0.0.1", Port{BASE + 61}) || !srv.listen(1)) {
                ready = true;
                return;
            }
            ready = true;
            auto peer = srv.accept();
            if (!peer) return;
            peer->setNoDelay(true);
            peer->setReceiveTimeout(Milliseconds{10000});
            size_t got = 0;
            while (got < TOTAL) {
                size_t want = std::min(CHUNK, TOTAL - got);
                if (!peer->receiveAll(recvBuf.data(), want)) break;
                got += want;
            }
            recvTotal = got;
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        auto c = TcpSocket::createRaw();
        REQUIRE(c.connect("127.0.0.1", Port{BASE + 61}));
        c.setNoDelay(true);

        auto t0 = std::chrono::steady_clock::now();
        size_t sent = 0;
        while (sent < TOTAL) {
            size_t want = std::min(CHUNK, TOTAL - sent);
            REQUIRE(c.sendAll(sendBuf.data(), want));
            sent += want;
        }
        c.close();
        srvThread.join();
        auto t1 = std::chrono::steady_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        double mbps
            = (static_cast<double>(TOTAL) / (1024.0 * 1024.0)) / (ms / 1000.0);

        std::cout << std::fixed << std::setprecision(1);
        std::cout << "  total: " << (TOTAL / (1024 * 1024))
                  << " MB  sent: " << sent
                  << " B  received: " << recvTotal.load() << " B\n";
        std::cout << "  time: " << ms << " ms  throughput: " << mbps
                  << " MB/s\n";

        REQUIRE(recvTotal.load() == TOTAL);
    }
}

// -----------------------------------------------------------------------
// 5. Span-based send / receive overloads
// -----------------------------------------------------------------------
static void test_span_overloads() {
    BEGIN_TEST("Span send/receive: TCP loopback echo via std::byte spans");
    {
        auto srv = TcpSocket::createRaw();
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 40}));
        REQUIRE(srv.listen(1));

        std::thread t([&]() {
            auto conn = srv.accept();
            if (!conn) return;
            conn->setReceiveTimeout(Milliseconds{2000});
            std::vector<std::byte> echoBuf(64);
            int r = conn->receive(
                Span<std::byte>{echoBuf.data(), echoBuf.size()});
            if (r > 0) {
                conn->send(Span<const std::byte>{
                    echoBuf.data(), static_cast<size_t>(r)});
            }
        });

        auto cli = TcpSocket::createRaw();
        cli.setReceiveTimeout(Milliseconds{2000});
        REQUIRE(cli.connect("127.0.0.1", Port{BASE + 40}));

        std::string payload = "span-hello";
        std::vector<std::byte> sendBuf(payload.size());
        for (size_t i = 0; i < payload.size(); ++i)
            sendBuf[i] = static_cast<std::byte>(payload[i]);

        int sent
            = cli.send(Span<const std::byte>{sendBuf.data(), sendBuf.size()});
        REQUIRE(sent == static_cast<int>(payload.size()));

        std::vector<std::byte> recvBuf(64);
        int recvd
            = cli.receive(Span<std::byte>{recvBuf.data(), recvBuf.size()});
        REQUIRE(recvd == static_cast<int>(payload.size()));

        std::string echoed(recvd, '\0');
        for (int i = 0; i < recvd; ++i)
            echoed[static_cast<size_t>(i)]
                = static_cast<char>(recvBuf[static_cast<size_t>(i)]);
        REQUIRE(echoed == payload);

        t.join();
    }

    BEGIN_TEST("Span sendTo/receiveFrom: UDP datagram with byte spans");
    {
        UdpSocket receiver;
        receiver.setReuseAddress(true);
        REQUIRE(receiver.bind("127.0.0.1", Port{BASE + 41}));
        receiver.setReceiveTimeout(Milliseconds{2000});

        UdpSocket sender;
        Endpoint dest{"127.0.0.1", Port{BASE + 41}, AddressFamily::IPv4};

        std::string msg = "span-udp";
        std::vector<std::byte> txBuf(msg.size());
        for (size_t i = 0; i < msg.size(); ++i)
            txBuf[i] = static_cast<std::byte>(msg[i]);

        int sent = sender.sendTo(
            Span<const std::byte>{txBuf.data(), txBuf.size()}, dest);
        REQUIRE(sent == static_cast<int>(msg.size()));

        std::vector<std::byte> rxBuf(64);
        Endpoint from;
        int recvd = receiver.receiveFrom(
            Span<std::byte>{rxBuf.data(), rxBuf.size()}, from);
        REQUIRE(recvd == static_cast<int>(msg.size()));

        std::string got(recvd, '\0');
        for (int i = 0; i < recvd; ++i)
            got[static_cast<size_t>(i)]
                = static_cast<char>(rxBuf[static_cast<size_t>(i)]);
        REQUIRE(got == msg);
    }
}

// -----------------------------------------------------------------------
// 6. Socket buffer sizes (SO_RCVBUF / SO_SNDBUF)
// -----------------------------------------------------------------------
static void test_buffer_sizes() {
    BEGIN_TEST("setReceiveBufferSize: succeeds on valid socket");
    {
        auto s = TcpSocket::createRaw();
        // 64 KiB is a commonly accepted value on all platforms.
        REQUIRE(s.setReceiveBufferSize(64 * 1024));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setSendBufferSize: succeeds on valid socket");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.setSendBufferSize(64 * 1024));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setReceiveBufferSize: succeeds for UDP socket");
    {
        UdpSocket s;
        REQUIRE(s.setReceiveBufferSize(128 * 1024));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setReceiveBufferSize: fails on closed socket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        REQUIRE(!s.setReceiveBufferSize(64 * 1024));
    }

    BEGIN_TEST("setSendBufferSize: fails on closed socket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        REQUIRE(!s.setSendBufferSize(64 * 1024));
    }
}

// -----------------------------------------------------------------------
// 7. shutdown(ShutdownHow)
// -----------------------------------------------------------------------
static void test_shutdown() {
    BEGIN_TEST("shutdown(Write): peer recv sees EOF (returns 0)");
    {
        auto srv = TcpSocket::createRaw();
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 20}));
        REQUIRE(srv.listen(1));

        std::atomic<int> peerRecv{-1};
        std::thread t([&]() {
            auto peer = srv.accept();
            if (peer) {
                peer->setReceiveTimeout(Milliseconds{2000});
                char buf[64];
                peerRecv = peer->receive(buf, sizeof(buf));
            }
        });

        auto c = TcpSocket::createRaw();
        REQUIRE(c.connect("127.0.0.1", Port{BASE + 20}));
        REQUIRE(c.shutdown(ShutdownHow::Write));
        t.join();
        // Peer should see 0 (clean EOF) or -1 (connection reset) — either way
        // it must have unblocked.
        REQUIRE_MSG(
            peerRecv >= 0, "peer recv unblocked after client shutdown(Write)");
    }

    BEGIN_TEST("shutdown(Both): socket remains isValid() after call");
    {
        auto srv = TcpSocket::createRaw();
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 21}));
        REQUIRE(srv.listen(1));

        // Signal so the client waits until accept() has been called; calling
        // shutdown(SHUT_RDWR) before the peer accepts can return ENOTCONN on
        // some kernels even though connect() already succeeded.
        std::atomic<bool> accepted{false};
        std::atomic<bool> clientDone{false};
        std::thread t([&]() {
            auto peer = srv.accept();
            accepted = true;
            // Keep the peer socket alive until the client has shut down,
            // so the client-side shutdown() doesn't hit a broken connection.
            auto deadline
                = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!clientDone && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            (void)peer;
        });

        auto c = TcpSocket::createRaw();
        REQUIRE(c.connect("127.0.0.1", Port{BASE + 21}));

        // Busy-wait with a short timeout for the server to accept.
        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!accepted && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        REQUIRE(c.shutdown(ShutdownHow::Both));
        REQUIRE(c.isValid()); // fd still open; only close() destroys it
        clientDone = true;
        t.join();
    }

    BEGIN_TEST("shutdown: fails on closed socket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        REQUIRE(!s.shutdown(ShutdownHow::Both));
    }
}

// -----------------------------------------------------------------------
// 8. setKeepAlive
// -----------------------------------------------------------------------
static void test_keepalive() {
    BEGIN_TEST("setKeepAlive(true): enables SO_KEEPALIVE");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.setKeepAlive(true));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setKeepAlive: can be disabled after enable");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.setKeepAlive(true));
        REQUIRE(s.setKeepAlive(false));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setKeepAlive: fails on closed socket");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        REQUIRE(!s.setKeepAlive(true));
    }
}

// -----------------------------------------------------------------------
int main() {
    test_endpoints();
    test_send_timeout();
    test_no_delay();
    test_udp();
    test_udp_connected();
    test_udp_transfer();
    test_bulk_throughput();
    test_span_overloads();
    test_buffer_sizes();
    test_shutdown();
    test_keepalive();
    return test_summary();
}
