// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests: SocketFactory API with Result<T> exception-free error handling
// Verifies that SocketFactory methods return Result<T> with proper error
// handling

#include "TcpSocket.h"
#include "UdpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>

using namespace aiSocks;

int main() {
    printf("=== SocketFactory API Tests ===\n");

    // Test 1: Basic socket creation
    BEGIN_TEST("SocketFactory::createTcpSocket succeeds");
    {
        auto result = SocketFactory::createTcpSocket();
        REQUIRE(result.isSuccess());
        auto& socket = result.value();
        REQUIRE(socket.isValid());
        REQUIRE(socket.getAddressFamily() == AddressFamily::IPv4);
    }

    // Test 2: IPv6 socket creation
    BEGIN_TEST("SocketFactory::createTcpSocket IPv6 succeeds");
    {
        auto result = SocketFactory::createTcpSocket(AddressFamily::IPv6);
        REQUIRE(result.isSuccess());
        auto& socket = result.value();
        REQUIRE(socket.isValid());
        REQUIRE(socket.getAddressFamily() == AddressFamily::IPv6);
    }

    // Test 3: UDP socket creation
    BEGIN_TEST("SocketFactory::createUdpSocket succeeds");
    {
        auto result = SocketFactory::createUdpSocket();
        REQUIRE(result.isSuccess());
        auto& socket = result.value();
        REQUIRE(socket.isValid());
        REQUIRE(socket.getAddressFamily() == AddressFamily::IPv4);
    }

    // Test 4: Server socket creation
    BEGIN_TEST("SocketFactory::createTcpServer succeeds");
    {
        auto result = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port::any});
        REQUIRE(result.isSuccess());
        auto& server = result.value();
        REQUIRE(server.isValid());

        // Server should be bound and listening
        auto endpoint = server.getLocalEndpoint();
        REQUIRE(endpoint.isSuccess());
        REQUIRE(endpoint.value().address == "127.0.0.1");
        REQUIRE(endpoint.value().port.value() != 0); // OS chose a real port
    }

    // Test 5: Client socket creation
    BEGIN_TEST("SocketFactory::createTcpClient fails on refused port");
    {
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{1}, Milliseconds{100}});
        REQUIRE(result.isError());
        REQUIRE(result.error() != SocketError::None);
        REQUIRE(!result.message().empty());
    }

    // Test 6: Client socket creation success
    BEGIN_TEST("SocketFactory::createTcpClient succeeds with real server");
    {
        // Bind synchronously so we know the port before spawning the thread.
        std::atomic<uint16_t> srvPort{0};
        // Start server in background
        std::thread server_thread([&srvPort]() {
            auto srv_result = SocketFactory::createTcpServer(
                ServerBind{"127.0.0.1", Port::any});
            if (srv_result.isSuccess()) {
                auto ep = srv_result.value().getLocalEndpoint();
                if (ep.isSuccess()) srvPort.store(ep.value().port.value());
                auto& srv = srv_result.value();
                auto client_result = srv.accept();
                if (client_result != nullptr) {
                    auto client = std::move(client_result);
                    char buf[256] = {};
                    int n = client->receive(buf, sizeof(buf) - 1);
                    if (n > 0) {
                        client->send(buf, n); // Echo back
                    }
                }
            }
        });

        // Wait for the server to bind and set its port.
        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (
            srvPort.load() == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        REQUIRE(srvPort.load() != 0);

        auto clt_result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{srvPort.load()}, Milliseconds{2000}});
        REQUIRE(clt_result.isSuccess());
        auto& client = clt_result.value();
        REQUIRE(client.isValid());

        // Test send/receive
        const char* msg = "Hello Factory!";
        bool sent = client.send(msg, strlen(msg));
        REQUIRE(sent);

        char buf[256] = {};
        int received = client.receive(buf, sizeof(buf) - 1);
        REQUIRE(received == static_cast<int>(strlen(msg)));
        REQUIRE(std::string(buf, received) == msg);

        server_thread.join();
    }

    // Test 7: Error handling with invalid address
    BEGIN_TEST("SocketFactory::createTcpClient fails on invalid address");
    {
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"invalid.address.that.does.not.exist", Port{80},
                Milliseconds{100}});
        REQUIRE(result.isError());
        REQUIRE(result.error() != SocketError::None);
    }

    // Test 8: Port in use error
    BEGIN_TEST("SocketFactory::createTcpServer fails on port in use");
    {
        // First server (Port::any — OS picks an ephemeral port)
        auto first_result = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port::any, Backlog{5}, false});
        REQUIRE(first_result.isSuccess());
        uint16_t p19902 = 0;
        {
            auto ep = first_result.value().getLocalEndpoint();
            p19902 = ep.isSuccess() ? ep.value().port.value() : 0;
        }
        REQUIRE(p19902 != 0);

        // Second server tries the same port without reuseAddr — must fail.
        auto second_result = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port{p19902}, Backlog{5}, false});
        REQUIRE(second_result.isError());
        REQUIRE(second_result.error() != SocketError::None);
    }

    // Test 9: createUdpServer binds correctly
    BEGIN_TEST("SocketFactory::createUdpServer succeeds and is bound");
    {
        auto result = SocketFactory::createUdpServer(
            ServerBind{"127.0.0.1", Port::any});
        REQUIRE(result.isSuccess());
        auto& sock = result.value();
        REQUIRE(sock.isValid());
        auto ep = sock.getLocalEndpoint();
        REQUIRE(ep.isSuccess());
        REQUIRE(ep.value().port.value() != 0);
    }

    // Test 10: createUdpServer + send/receive round-trip via factory
    BEGIN_TEST("SocketFactory::createUdpServer: client sends, server receives");
    {
        auto srv_result = SocketFactory::createUdpServer(
            ServerBind{"127.0.0.1", Port::any});
        REQUIRE(srv_result.isSuccess());
        auto& srv = srv_result.value();
        REQUIRE(srv.setReceiveTimeout(Milliseconds{2000}));

        auto ep = srv.getLocalEndpoint();
        REQUIRE(ep.isSuccess());
        Endpoint dest{"127.0.0.1", ep.value().port, AddressFamily::IPv4};

        // Client created via factory (exercises the impl-injection path)
        auto cli_result = SocketFactory::createUdpSocket();
        REQUIRE(cli_result.isSuccess());
        auto& cli = cli_result.value();

        const char* msg = "hello-udp-factory";
        int sent = cli.sendTo(msg, strlen(msg), dest);
        REQUIRE(sent == static_cast<int>(strlen(msg)));

        char buf[64] = {};
        Endpoint from;
        int received = srv.receiveFrom(buf, sizeof(buf), from);
        REQUIRE(received == static_cast<int>(strlen(msg)));
        REQUIRE(std::string(buf, static_cast<size_t>(received)) == msg);
    }

    // Test 11: createUdpServer bidirectional echo via factory
    BEGIN_TEST("SocketFactory::createUdpServer: bidirectional echo");
    {
        auto srv_result = SocketFactory::createUdpServer(
            ServerBind{"127.0.0.1", Port::any});
        REQUIRE(srv_result.isSuccess());
        auto& srv = srv_result.value();
        REQUIRE(srv.setReceiveTimeout(Milliseconds{2000}));

        auto ep = srv.getLocalEndpoint();
        REQUIRE(ep.isSuccess());
        Endpoint srvAddr{"127.0.0.1", ep.value().port, AddressFamily::IPv4};

        auto cli_result = SocketFactory::createUdpSocket();
        REQUIRE(cli_result.isSuccess());
        auto& cli = cli_result.value();
        REQUIRE(cli.setReceiveTimeout(Milliseconds{2000}));

        for (int i = 0; i < 5; ++i) {
            std::string out = "ping-" + std::to_string(i);

            REQUIRE(cli.sendTo(out.data(), out.size(), srvAddr)
                == static_cast<int>(out.size()));

            char recvBuf[64] = {};
            Endpoint from;
            int r = srv.receiveFrom(recvBuf, sizeof(recvBuf), from);
            REQUIRE(r == static_cast<int>(out.size()));

            // Server echoes back
            srv.sendTo(recvBuf, static_cast<size_t>(r), from);

            char echoBuf[64] = {};
            Endpoint ignored;
            int er = cli.receiveFrom(echoBuf, sizeof(echoBuf), ignored);
            REQUIRE(er == static_cast<int>(out.size()));
            REQUIRE(std::string(echoBuf, static_cast<size_t>(er)) == out);
        }
    }

    return test_summary();
}
