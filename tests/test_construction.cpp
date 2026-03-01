// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
//
// Tests: SocketFactory API with Result<T> exception-free error handling
// Verifies that SocketFactory methods return Result<T> with proper error
// handling

#include "TcpSocket.h"
#include "UdpSocket.h"
#include "SocketFactory.h"
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
    BEGIN_TEST("Basic factory: TCP/IPv4 succeeds");
    {
        auto result = SocketFactory::createTcpSocket();
        REQUIRE(result.isSuccess());
        REQUIRE(result.value().isValid());
        REQUIRE(result.value().getAddressFamily() == AddressFamily::IPv4);
    }

    BEGIN_TEST("Basic factory: all type/family combos succeed");
    {
        auto a = SocketFactory::createTcpSocket();
        auto b = SocketFactory::createTcpSocket(AddressFamily::IPv6);
        auto c = SocketFactory::createUdpSocket();
        auto d = SocketFactory::createUdpSocket(AddressFamily::IPv6);

        REQUIRE(a.isSuccess());
        REQUIRE(b.isSuccess());
        REQUIRE(c.isSuccess());
        REQUIRE(d.isSuccess());

        REQUIRE(a.value().getAddressFamily() == AddressFamily::IPv4);
        REQUIRE(b.value().getAddressFamily() == AddressFamily::IPv6);
        REQUIRE(c.value().getAddressFamily() == AddressFamily::IPv4);
        REQUIRE(d.value().getAddressFamily() == AddressFamily::IPv6);
    }
}

// -----------------------------------------------------------------------
// Happy paths  ServerBind constructor
// -----------------------------------------------------------------------
static void test_server_bind_happy() {
    BEGIN_TEST("ServerBind factory: socket is valid and ready to accept");
    {
        auto result = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port{BASE}});
        REQUIRE(result.isSuccess());
        auto& s = result.value();
        REQUIRE(s.isValid());
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("ServerBind factory: can immediately accept a connection");
    {
        auto srv_result = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port{BASE + 1}});
        REQUIRE(srv_result.isSuccess());
        auto& srv = srv_result.value();
        REQUIRE(srv.isValid());

        std::string cltError;
        std::thread clt([&]() {
            auto clt_result = SocketFactory::createTcpClient(
                AddressFamily::IPv4,
                ConnectArgs{"127.0.0.1", Port{BASE + 1}, Milliseconds{1000}});
            if (clt_result.isError()) {
                cltError = clt_result.message();
            }
        });

        auto peer_result = srv.accept();
        clt.join();
        REQUIRE(cltError.empty());
        REQUIRE(peer_result != nullptr);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST(
        "ServerBind factory: reuseAddr=false still works on a fresh port");
    {
        auto result = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port{BASE + 2}, Backlog{5}, false});
        REQUIRE(result.isSuccess());
        auto& s = result.value();
        REQUIRE(s.isValid());
    }
}

// -----------------------------------------------------------------------
// Happy paths  ConnectArgs constructor
// -----------------------------------------------------------------------
static void test_connect_to_happy() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("ConnectArgs factory: creates a connected socket");
    {
        std::atomic<bool> ready{false};

        std::thread srvThread([&]() {
            auto srv_result = SocketFactory::createTcpServer(
                ServerBind{"127.0.0.1", Port{BASE + 3}});
            if (srv_result.isError()) {
                return;
            }
            auto& srv = srv_result.value();
            ready = true;
            auto peer_result = srv.accept();
            // peer closes on scope exit; isValid() on the client side
            // checks the fd, not the liveness of the peer
        });

        // Wait for server
        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        auto clt_result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{BASE + 3}, Milliseconds{1000}});
        srvThread.join();
        REQUIRE(clt_result.isSuccess());
        REQUIRE(clt_result.value().isValid());
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("ConnectArgs factory: send/receive works immediately after "
               "construction");
    {
        const std::string payload = "hello-from-constructor";
        std::atomic<bool> ready{false};
        std::string received;

        std::thread srvThread([&]() {
            auto srv_result = SocketFactory::createTcpServer(
                ServerBind{"127.0.0.1", Port{BASE + 4}});
            if (srv_result.isError()) {
                ready = true;
                return;
            }
            auto& srv = srv_result.value();
            ready = true;
            auto peer_result = srv.accept();
            if (peer_result != nullptr) {
                auto peer = std::move(peer_result);
                char buf[256] = {};
                int r = peer->receive(buf, sizeof(buf) - 1);
                if (r > 0) received.assign(buf, r);
                peer->close();
            }
        });

        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!ready && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        auto clt_result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{BASE + 4}, Milliseconds{1000}});
        REQUIRE(clt_result.isSuccess());
        auto& c = clt_result.value();
        bool sent = c.send(payload.data(), payload.size());
        srvThread.join();
        REQUIRE(sent);
        REQUIRE(received == payload);
    }
}

// -----------------------------------------------------------------------
// Unhappy paths  Result<T> errors on construction failure
// -----------------------------------------------------------------------
static void test_server_bind_failures() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("ServerBind factory: returns error on port-in-use (same port, "
               "no reuseAddr)");
    {
        // First socket holds the port
        auto first_result = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port{BASE + 10}, Backlog{5}, false});
        REQUIRE(first_result.isSuccess());
        auto& first = first_result.value();

        // Second socket tries same port without reuseAddr
        auto second_result = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port{BASE + 10}, Backlog{5}, false});
        REQUIRE(second_result.isError());
        REQUIRE(second_result.error() != SocketError::None);
        REQUIRE(!second_result.message().empty());
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("ServerBind factory: returns error on invalid address");
    {
        auto result = SocketFactory::createTcpServer(
            ServerBind{"invalid.address.that.does.not.exist", Port{BASE + 11}});
        REQUIRE(result.isError());
        REQUIRE(result.error() != SocketError::None);
        REQUIRE(!result.message().empty());
    }
}

// -----------------------------------------------------------------------
// Unhappy paths  ConnectArgs failures
// -----------------------------------------------------------------------
static void test_connect_to_failures() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    BEGIN_TEST("ConnectArgs factory: returns error on refused port");
    {
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{1}, Milliseconds{100}});
        REQUIRE(result.isError());
        REQUIRE(result.error() != SocketError::None);
        REQUIRE(!result.message().empty());
    }

    BEGIN_TEST("ConnectArgs factory: returns error on invalid address");
    {
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"invalid.address.that.does.not.exist", Port{80},
                Milliseconds{100}});
        REQUIRE(result.isError());
        REQUIRE(result.error() != SocketError::None);
        REQUIRE(!result.message().empty());
    }

    BEGIN_TEST("ConnectArgs factory: returns error on timeout");
    {
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{
                "10.255.255.1", Port{80}, Milliseconds{10}}); // Non-routable IP
        REQUIRE(result.isError());
        // Should timeout or be unreachable
        REQUIRE(result.error() == SocketError::Timeout
            || result.error() == SocketError::ConnectFailed);
    }
}

// -----------------------------------------------------------------------
// Move semantics
// -----------------------------------------------------------------------
static void test_move_semantics() {
    BEGIN_TEST("SocketFactory result can be moved");
    {
        auto result1 = SocketFactory::createTcpSocket();
        REQUIRE(result1.isSuccess());

        auto result2 = std::move(result1);
        REQUIRE(result2.isSuccess());
        REQUIRE(result2.value().isValid());
    }

    BEGIN_TEST("SocketFactory created socket can be moved");
    {
        auto result = SocketFactory::createTcpSocket();
        REQUIRE(result.isSuccess());

        TcpSocket sock1 = std::move(result.value());
        REQUIRE(sock1.isValid());

        TcpSocket sock2 = std::move(sock1);
        REQUIRE(sock2.isValid());
    }
}

int main() {
    std::cout << "=== SocketFactory Construction Tests ===\n";

    test_basic_constructor();
    test_server_bind_happy();
    test_connect_to_happy();
    test_server_bind_failures();
    test_connect_to_failures();
    test_move_semantics();

    std::cout << "All construction tests passed!\n";
    return 0;
}
