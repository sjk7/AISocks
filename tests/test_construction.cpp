// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
//
// Tests: correct-by-construction Socket API. Verifies
// that constructors throw SocketException on failure and produce a fully usable
// socket on success.

#include "TcpSocket.h"
#include "UdpSocket.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <string>
#include <cstring>

using namespace aiSocks;

static const uint16_t BASE = 19900;

// -----------------------------------------------------------------------
// Happy paths  basic constructor
// -----------------------------------------------------------------------
static void test_basic_constructor() {
    BEGIN_TEST("Basic ctor: TCP/IPv4 does not throw");
    {
        bool threw = false;
        try {
            auto s = TcpSocket::createRaw();
        } catch (...) {
            threw = true;
        }
        REQUIRE(!threw);
    }

    BEGIN_TEST("Basic ctor: all type/family combos succeed");
    {
        bool threw = false;
        try {
            auto a = TcpSocket::createRaw();
            auto b = TcpSocket::createRaw(AddressFamily::IPv6);
            UdpSocket c;
            UdpSocket d(AddressFamily::IPv6);
        } catch (...) {
            threw = true;
        }
        REQUIRE(!threw);
    }
}

// -----------------------------------------------------------------------
// Happy paths  ServerBind constructor
// -----------------------------------------------------------------------
static void test_server_bind_happy() {
    BEGIN_TEST("ServerBind ctor: socket is valid and ready to accept");
    {
        bool threw = false;
        try {
            TcpSocket s(AddressFamily::IPv4,
                ServerBind{"127.0.0.1", Port{BASE}});
            REQUIRE(s.isValid());
        } catch (const SocketException& e) {
            std::cerr << "  Unexpected exception: " << e.what() << "\n";
            threw = true;
        }
        REQUIRE(!threw);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("ServerBind ctor: can immediately accept a connection");
    {
        bool threw = false;
        std::atomic<bool> ready{false};
        try {
            TcpSocket srv(AddressFamily::IPv4,
                ServerBind{"127.0.0.1", Port{BASE + 1}});
            REQUIRE(srv.isValid());
            ready = true;

            std::string cltError;
            std::thread clt([&]() {
                // server is already blocking on accept(); connect immediately
                try {
                    TcpSocket c(AddressFamily::IPv4,
                        ConnectTo{"127.0.0.1", Port{BASE + 1}});
                    // peer closes on scope exit  accept() on server side has
                    // already returned by the time connect() completes
                } catch (const std::exception& e) {
                    cltError = e.what();
                }
            });

            auto peer = srv.accept();
            clt.join();
            REQUIRE(cltError.empty());
            REQUIRE(peer != nullptr);
        } catch (const SocketException& e) {
            std::cerr << "  Unexpected exception: " << e.what() << "\n";
            threw = true;
        }
        REQUIRE(!threw);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("ServerBind ctor: reuseAddr=false still works on a fresh port");
    {
        bool threw = false;
        try {
            TcpSocket s(AddressFamily::IPv4,
                ServerBind{"127.0.0.1", Port{BASE + 2}, 5, false});
            REQUIRE(s.isValid());
        } catch (const SocketException& e) {
            std::cerr << "  Unexpected exception: " << e.what() << "\n";
            threw = true;
        }
        REQUIRE(!threw);
    }
}

// -----------------------------------------------------------------------
// Happy paths  ConnectTo constructor
// -----------------------------------------------------------------------
static void test_connect_to_happy() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("ConnectTo ctor: creates a connected socket");
    {
        std::atomic<bool> ready{false};
        std::atomic<bool> threw{false};

        std::thread srvThread([&]() {
            try {
                TcpSocket srv(AddressFamily::IPv4,
                    ServerBind{"127.0.0.1", Port{BASE + 3}});
                ready = true;
                auto peer = srv.accept();
                // peer closes on scope exit; isValid() on the client side
                // checks the fd, not the liveness of the peer
            } catch (...) {
                ready = true;
            }
        });

        // Wait for server
        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        try {
            TcpSocket c(AddressFamily::IPv4,
                ConnectTo{"127.0.0.1", Port{BASE + 3}});
            REQUIRE(c.isValid());
        } catch (const SocketException& e) {
            std::cerr << "  Unexpected exception: " << e.what() << "\n";
            threw = true;
        }
        srvThread.join();
        REQUIRE(!threw);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST(
        "ConnectTo ctor: send/receive works immediately after construction");
    {
        const std::string payload = "hello-from-constructor";
        std::atomic<bool> ready{false};
        std::string received;

        std::thread srvThread([&]() {
            try {
                TcpSocket srv(AddressFamily::IPv4,
                    ServerBind{"127.0.0.1", Port{BASE + 4}});
                ready = true;
                auto peer = srv.accept();
                if (peer) {
                    char buf[256] = {};
                    int r = peer->receive(buf, sizeof(buf) - 1);
                    if (r > 0) received.assign(buf, r);
                    peer->close();
                }
            } catch (...) {
                ready = true;
            }
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        bool threw = false;
        try {
            TcpSocket c(AddressFamily::IPv4,
                ConnectTo{"127.0.0.1", Port{BASE + 4}});
            c.send(payload.data(), payload.size());
        } catch (const SocketException& e) {
            std::cerr << "  Unexpected exception: " << e.what() << "\n";
            threw = true;
        }
        srvThread.join();
        REQUIRE(!threw);
        REQUIRE(received == payload); //-V547
    }
}

// -----------------------------------------------------------------------
// Unhappy paths  exceptions on construction failure
// -----------------------------------------------------------------------
static void test_server_bind_failures() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("ServerBind ctor: throws SocketException on port-in-use (same "
               "port, no reuseAddr)");
    {
        // First socket holds the port
        TcpSocket first(AddressFamily::IPv4,
            ServerBind{"127.0.0.1", Port{BASE + 10}, 5, false});

        bool threw = false;
        SocketError code = SocketError::None;
        std::string what;
        try {
            TcpSocket second(AddressFamily::IPv4,
                ServerBind{"127.0.0.1", Port{BASE + 10}, 5, false});
        } catch (const SocketException& e) {
            threw = true;
            code = e.errorCode();
            what = e.what();
        }
        REQUIRE(threw);
        REQUIRE(code == SocketError::BindFailed);
        // Message should contain the step name and a non-empty OS description
        REQUIRE_MSG(what.find("bind(") != std::string::npos,
            "exception message contains 'bind(' step");
        REQUIRE_MSG(what.size() > 10, "exception message is substantive");
        std::cout << "  exception message: " << what << "\n";
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST(
        "ServerBind ctor: throws SocketException on invalid bind address");
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
        // BindFailed is the expected code for a bad address
        REQUIRE(code == SocketError::BindFailed);
    }

    BEGIN_TEST(
        "ServerBind ctor: exception message contains OS error description");
    {
        // Reuse the already-bound port scenario to reliably get a failure
        TcpSocket occupant(AddressFamily::IPv4,
            ServerBind{"127.0.0.1", Port{BASE + 12}, 5, false});
        std::string what;
        try {
            TcpSocket s(AddressFamily::IPv4,
                ServerBind{"127.0.0.1", Port{BASE + 12}, 5, false});
        } catch (const SocketException& e) {
            what = e.what();
        }
        // Should contain something that looks like an OS error (non-whitespace
        // after '[')
        bool hasOsText = (what.find('[') != std::string::npos)
            && (what.size() > what.find('[') + 3);
        REQUIRE_MSG(hasOsText,
            "exception message contains OS error text in [...] format");
        std::cout << "  full message: " << what << "\n";
    }
}

static void test_connect_to_failures() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST(
        "ConnectTo ctor: throws SocketException when nothing is listening");
    {
        bool threw = false;
        SocketError code = SocketError::None;
        std::string what;
        try {
            // Port 1 is virtually never listening and requires no privilege to
            // attempt
            TcpSocket c(AddressFamily::IPv4,
                ConnectTo{"127.0.0.1", Port{1}});
        } catch (const SocketException& e) {
            threw = true;
            code = e.errorCode();
            what = e.what();
        }
        REQUIRE(threw);
        REQUIRE(code == SocketError::ConnectFailed);
        REQUIRE_MSG(what.find("connect(") != std::string::npos,
            "exception message contains 'connect(' step");
        std::cout << "  exception message: " << what << "\n";
    }

    BEGIN_TEST("ConnectTo ctor: throws SocketException on bad numeric address");
    {
        // 999.999.999.999 is rejected immediately by inet_pton; getaddrinfo
        // also fails quickly as it cannot be a valid hostname.
        bool threw = false;
        SocketError code = SocketError::None;
        try {
            TcpSocket c(AddressFamily::IPv4,
                ConnectTo{"999.999.999.999", Port{BASE + 20}});
        } catch (const SocketException& e) {
            threw = true;
            code = e.errorCode();
        }
        REQUIRE(threw);
        REQUIRE(code == SocketError::ConnectFailed);
    }

    // NOTE: DNS resolution is synchronous in this single-threaded library.
    // connectTimeout only covers the TCP handshake phase, not DNS.
    // The .invalid TLD (RFC 2606) is guaranteed never to resolve; most OS
    // resolvers return NXDOMAIN quickly without a full DNS round-trip.
    BEGIN_TEST(
        "ConnectTo ctor: throws SocketException on unresolvable hostname");
    {
        bool threw = false;
        SocketError code = SocketError::None;
        try {
            TcpSocket c(AddressFamily::IPv4,
                ConnectTo{"this.host.does.not.exist.invalid", Port{BASE + 21}});
        } catch (const SocketException& e) {
            threw = true;
            code = e.errorCode();
        }
        REQUIRE(threw);
        REQUIRE(code == SocketError::ConnectFailed);
    }

    // 10.255.255.1 is an unassigned address in the private 10/8 range that
    // is not reachable on this network  SYNs are silently dropped,
    // so a blocking connect would hang indefinitely without a timeout.
    // (192.0.2.0/24 RFC 5737 TEST-NET is routable on this host via VPN.)
    static constexpr const char* nonRouteableIP = "10.255.255.1";
    BEGIN_TEST("ConnectTo ctor: connectTimeout fires for non-routable address");
    {
        using clock = std::chrono::steady_clock;
        constexpr int TIMEOUT_MS = 50;

        auto t0 = clock::now();
        bool threw = false;
        SocketError code = SocketError::None;
        std::string what;
        try {
            TcpSocket c(AddressFamily::IPv4,
                ConnectTo{nonRouteableIP, Port{9}, Milliseconds{TIMEOUT_MS}});
        } catch (const SocketException& e) {
            threw = true;
            code = e.errorCode();
            what = e.what();
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - t0)
                           .count();

        if (!threw) {
            // Address was reachable on this host (VPN / unexpected route).
            REQUIRE_MSG(true,
                "SKIP - " + std::string(nonRouteableIP)
                    + " is routable on this host; timeout test not applicable");
        } else {
            REQUIRE_MSG(code == SocketError::Timeout
                    || code == SocketError::ConnectFailed,
                "error code is Timeout or ConnectFailed");
            REQUIRE_MSG(elapsed < TIMEOUT_MS * 3,
                "returned within 3x the requested timeout");
            std::cout << "  code=" << static_cast<int>(code)
                      << "  elapsed=" << elapsed << "ms  msg: " << what << "\n";
        }
    }
}

static void test_exception_is_std_exception() {
    BEGIN_TEST("SocketException is catchable as std::exception");
    {
        bool caught = false;
        try {
            TcpSocket c(AddressFamily::IPv4,
                ConnectTo{"127.0.0.1", Port{1}});
        } catch (const std::exception& e) {
            caught = true;
            REQUIRE_MSG(std::string(e.what()).size() > 0,
                "std::exception::what() is non-empty");
        }
        REQUIRE(caught);
    }

    BEGIN_TEST(
        "SocketException carries correct error code after ConnectFailed");
    {
        SocketError code = SocketError::None;
        try {
            TcpSocket c(AddressFamily::IPv4,
                ConnectTo{"127.0.0.1", Port{1}});
        } catch (const SocketException& e) {
            code = e.errorCode();
        }
        REQUIRE(code == SocketError::ConnectFailed);
    }
}

// -----------------------------------------------------------------------
// Move semantics still work with throwing constructors
// -----------------------------------------------------------------------
static void test_move_after_server_bind() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("ServerBind socket can be move-constructed");
    {
        bool threw = false;
        try {
            TcpSocket s(AddressFamily::IPv4,
                ServerBind{"127.0.0.1", Port{BASE + 30}});
            TcpSocket moved(std::move(s));
            REQUIRE(moved.isValid());
            REQUIRE(!s.isValid());
        } catch (const SocketException& e) {
            std::cerr << "  Unexpected: " << e.what() << "\n";
            threw = true;
        }
        REQUIRE(!threw);
    }
}

int main() {
    std::cout << "=== Constructor Correctness Tests ===\n";

    using clock = std::chrono::steady_clock;
    auto time = [&](const char* name, auto fn) {
        auto t0 = clock::now();
        fn();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - t0)
                      .count();
        std::cout << "  [timing] " << name << ": " << ms << " ms\n";
    };

    time("test_basic_constructor", test_basic_constructor);
    time("test_server_bind_happy", test_server_bind_happy);
    time("test_connect_to_happy", test_connect_to_happy);
    time("test_server_bind_failures", test_server_bind_failures);
    time("test_connect_to_failures", test_connect_to_failures);
    time("test_exception_is_std_exception", test_exception_is_std_exception);
    time("test_move_after_server_bind", test_move_after_server_bind);

    return test_summary();
}
