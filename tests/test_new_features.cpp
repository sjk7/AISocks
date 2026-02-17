// Tests for features added in the second pass:
//   Endpoint / getLocalEndpoint / getPeerEndpoint
//   setSendTimeout
//   setNoDelay (TCP_NODELAY)
//   UDP sendTo / receiveFrom
//   shutdown(ShutdownHow)
//   setKeepAlive (SO_KEEPALIVE)

#include "Socket.h"
#include "test_helpers.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using namespace aiSocks;

// Port block: 20000 – 20099 (no overlap with existing test suites)
static constexpr int BASE = 20000;

// -----------------------------------------------------------------------
// 1. Endpoint / getLocalEndpoint / getPeerEndpoint
// -----------------------------------------------------------------------
static void test_endpoints() {
    BEGIN_TEST("getLocalEndpoint: address and port correct after bind");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        s.setReuseAddress(true);
        REQUIRE(s.bind("127.0.0.1", Port{BASE}));
        auto ep = s.getLocalEndpoint();
        REQUIRE(ep.has_value());
        REQUIRE(ep->port == Port{BASE});
        REQUIRE(ep->address == "127.0.0.1");
        REQUIRE(ep->family == AddressFamily::IPv4);
    }

    BEGIN_TEST(
        "getLocalEndpoint: ephemeral port non-zero after bind on port 0");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(s.bind("127.0.0.1", Port{0}));
        auto ep = s.getLocalEndpoint();
        REQUIRE(ep.has_value());
        REQUIRE(ep->port.value != 0);
        std::cout << "  assigned ephemeral port: " << ep->port.value << "\n";
    }

    BEGIN_TEST("getPeerEndpoint: populated after TCP connect");
    {
        Socket srv(SocketType::TCP, AddressFamily::IPv4);
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 1}));
        REQUIRE(srv.listen(1));

        std::thread t([&]() {
            auto peer = srv.accept();
            // peer goes out of scope; connection closes
        });

        Socket c(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(c.connect("127.0.0.1", Port{BASE + 1}));
        auto ep = c.getPeerEndpoint();
        REQUIRE(ep.has_value());
        REQUIRE(ep->port == Port{BASE + 1});
        REQUIRE(ep->address == "127.0.0.1");
        t.join();
    }

    BEGIN_TEST("getLocalEndpoint: nullopt on closed socket");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        s.close();
        auto ep = s.getLocalEndpoint();
        REQUIRE(!ep.has_value());
    }

    BEGIN_TEST("getPeerEndpoint: nullopt on unconnected socket");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        s.bind("127.0.0.1", Port{0});
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
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(s.setSendTimeout(Milliseconds{5000}));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setSendTimeout: Milliseconds{0} disables timeout");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(s.setSendTimeout(Milliseconds{0}));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setSendTimeout: fails on closed socket");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
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
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(s.setNoDelay(true));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setNoDelay: can be toggled off");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(s.setNoDelay(true));
        REQUIRE(s.setNoDelay(false));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setNoDelay: fails on closed socket");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
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
        Socket receiver(SocketType::UDP, AddressFamily::IPv4);
        receiver.setReuseAddress(true);
        REQUIRE(receiver.bind("127.0.0.1", Port{BASE + 10}));

        Socket sender(SocketType::UDP, AddressFamily::IPv4);

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
        Socket srv(SocketType::UDP, AddressFamily::IPv4);
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 11}));

        Socket cli(SocketType::UDP, AddressFamily::IPv4);
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
// 5. shutdown(ShutdownHow)
// -----------------------------------------------------------------------
static void test_shutdown() {
    BEGIN_TEST("shutdown(Write): peer recv sees EOF (returns 0)");
    {
        Socket srv(SocketType::TCP, AddressFamily::IPv4);
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 20}));
        REQUIRE(srv.listen(1));

        std::atomic<int> peerRecv{-1};
        std::thread t([&]() {
            auto peer = srv.accept();
            if (peer) {
                char buf[64];
                peerRecv = peer->receive(buf, sizeof(buf));
            }
        });

        Socket c(SocketType::TCP, AddressFamily::IPv4);
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
        Socket srv(SocketType::TCP, AddressFamily::IPv4);
        srv.setReuseAddress(true);
        REQUIRE(srv.bind("127.0.0.1", Port{BASE + 21}));
        REQUIRE(srv.listen(1));

        std::thread t([&]() { srv.accept(); });

        Socket c(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(c.connect("127.0.0.1", Port{BASE + 21}));
        REQUIRE(c.shutdown(ShutdownHow::Both));
        REQUIRE(c.isValid()); // fd still open; only close() destroys it
        t.join();
    }

    BEGIN_TEST("shutdown: fails on closed socket");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        s.close();
        REQUIRE(!s.shutdown(ShutdownHow::Both));
    }
}

// -----------------------------------------------------------------------
// 6. setKeepAlive
// -----------------------------------------------------------------------
static void test_keepalive() {
    BEGIN_TEST("setKeepAlive(true): enables SO_KEEPALIVE");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(s.setKeepAlive(true));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setKeepAlive: can be disabled after enable");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(s.setKeepAlive(true));
        REQUIRE(s.setKeepAlive(false));
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("setKeepAlive: fails on closed socket");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
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
    test_shutdown();
    test_keepalive();
    return test_summary();
}
