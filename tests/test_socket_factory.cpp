// This is a personal academic project. Dear PVS-Studio, please check it.
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

using namespace aiSocks;

int main() {
    std::cout << "=== SocketFactory API Tests ===\n";

    // Test 1: Basic socket creation
    BEGIN_TEST("SocketFactory::createTcpSocket succeeds");
    {
        auto result = SocketFactory::createTcpSocket();
        REQUIRE(result.isSuccess());
        REQUIRE(result.value().isValid());
        REQUIRE(result.value().getAddressFamily() == AddressFamily::IPv4);
    }

    // Test 2: IPv6 socket creation
    BEGIN_TEST("SocketFactory::createTcpSocket IPv6 succeeds");
    {
        auto result = SocketFactory::createTcpSocket(AddressFamily::IPv6);
        REQUIRE(result.isSuccess());
        REQUIRE(result.value().isValid());
        REQUIRE(result.value().getAddressFamily() == AddressFamily::IPv6);
    }

    // Test 3: UDP socket creation
    BEGIN_TEST("SocketFactory::createUdpSocket succeeds");
    {
        auto result = SocketFactory::createUdpSocket();
        REQUIRE(result.isSuccess());
        REQUIRE(result.value().isValid());
        REQUIRE(result.value().getAddressFamily() == AddressFamily::IPv4);
    }

    // Test 4: Server socket creation
    BEGIN_TEST("SocketFactory::createTcpServer succeeds");
    {
        auto result = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port{19900}});
        REQUIRE(result.isSuccess());
        auto& server = result.value();
        REQUIRE(server.isValid());

        // Server should be bound and listening
        auto endpoint = server.getLocalEndpoint();
        REQUIRE(endpoint.isSuccess());
        REQUIRE(endpoint.value().address == "127.0.0.1");
        REQUIRE(endpoint.value().port == Port{19900});
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
        // Start server in background
        std::thread server_thread([]() {
            auto srv_result = SocketFactory::createTcpServer(
                ServerBind{"127.0.0.1", Port{19901}});
            if (srv_result.isSuccess()) {
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

        // Give server time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto clt_result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{19901}, Milliseconds{1000}});
        REQUIRE(clt_result.isSuccess());
        auto& client = clt_result.value();
        REQUIRE(client.isValid());

        // Test send/receive
        const char* msg = "Hello Factory!";
        bool sent = client.send(msg, std::strlen(msg));
        REQUIRE(sent);

        char buf[256] = {};
        int received = client.receive(buf, sizeof(buf) - 1);
        REQUIRE(received == static_cast<int>(std::strlen(msg)));
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
        // First server
        auto first_result = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port{19902}, Backlog{5}, false});
        REQUIRE(first_result.isSuccess());

        // Second server tries same port without reuseAddr
        auto second_result = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port{19902}, Backlog{5}, false});
        REQUIRE(second_result.isError());
        REQUIRE(second_result.error() != SocketError::None);
    }

    std::cout << "All SocketFactory tests passed!\n";
    return 0;
}
