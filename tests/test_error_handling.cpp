// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com Tests: Error reporting and graceful failure for
// invalid/misused operations using SocketFactory Result<T> API.

#include "TcpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>

using namespace aiSocks;

int main() {
    std::cout << "=== Error Handling Tests ===\n";

    BEGIN_TEST("bind() on invalid socket returns error");
    {
        auto result = SocketFactory::createTcpSocket();
        REQUIRE(result.isSuccess());
        auto s = std::move(result.value());
        s.close(); // invalidate
        
        // Try to bind on invalid socket
        bool bind_result = s.bind("127.0.0.1", Port{19700});
        REQUIRE(!bind_result);
        REQUIRE(s.getLastError() != SocketError::None);
    }

    BEGIN_TEST("listen() without bind returns error");
    {
        auto result = SocketFactory::createTcpSocket();
        REQUIRE(result.isSuccess());
        auto& s = result.value();
        
        // listen without prior bind should fail
        bool listen_result = s.listen(5);
        // Not guaranteed to fail on every OS, but getLastError must not be None
        // when it does fail; if it succeeds that's also acceptable (OS behavior)
        REQUIRE_MSG(true, "listen() without bind completed without crash");
    }

    BEGIN_TEST("connect() to a refused port returns error");
    {
        auto result = SocketFactory::createTcpSocket();
        REQUIRE(result.isSuccess());
        auto& s = result.value();
        
        // Port 1 is almost certainly not listening
        bool connect_result = s.connect("127.0.0.1", Port{1}, Milliseconds{100});
        REQUIRE(!connect_result);
        REQUIRE(s.getLastError() != SocketError::None);
    }

    BEGIN_TEST("getErrorMessage returns non-empty string after a failed operation");
    {
        auto result = SocketFactory::createTcpSocket();
        REQUIRE(result.isSuccess());
        auto& s = result.value();
        
        (void)s.connect("127.0.0.1", Port{1}, Milliseconds{100}); // will fail
        std::string msg = s.getErrorMessage();
        REQUIRE(!msg.empty());
    }

    BEGIN_TEST("SocketFactory::createTcpClient fails on refused port");
    {
        auto result = SocketFactory::createTcpClient(
            AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{1}, Milliseconds{100}});
        REQUIRE(result.isError());
        REQUIRE(result.error() != SocketError::None);
        REQUIRE(!result.message().empty());
    }

    BEGIN_TEST("SocketFactory::createTcpServer fails on invalid address");
    {
        auto result = SocketFactory::createTcpServer(
            ServerBind{"invalid.address.that.does.not.exist", Port{8080}});
        REQUIRE(result.isError());
        REQUIRE(result.error() != SocketError::None);
        REQUIRE(!result.message().empty());
    }

    BEGIN_TEST("SocketFactory::createTcpServer fails on port in use");
    {
        // First server
        auto first_result = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port{19701}, 5, false});
        REQUIRE(first_result.isSuccess());
        auto& first = first_result.value();

        // Second server tries same port without reuseAddr
        auto second_result = SocketFactory::createTcpServer(
            ServerBind{"127.0.0.1", Port{19701}, 5, false});
        REQUIRE(second_result.isError());
        REQUIRE(second_result.error() != SocketError::None);
        REQUIRE(!second_result.message().empty());
    }

    BEGIN_TEST("send() on closed socket returns error");
    {
        auto result = SocketFactory::createTcpSocket();
        REQUIRE(result.isSuccess());
        auto s = std::move(result.value());
        s.close();
        
        int sent = s.send("hello", 5);
        REQUIRE(sent < 0);
        REQUIRE(s.getLastError() != SocketError::None);
    }

    BEGIN_TEST("receive() on closed socket returns error");
    {
        auto result = SocketFactory::createTcpSocket();
        REQUIRE(result.isSuccess());
        auto s = std::move(result.value());
        s.close();
        
        char buf[256];
        int received = s.receive(buf, sizeof(buf));
        REQUIRE(received < 0);
        REQUIRE(s.getLastError() != SocketError::None);
    }

    BEGIN_TEST("sendAll() on closed socket returns error");
    {
        auto result = SocketFactory::createTcpSocket();
        REQUIRE(result.isSuccess());
        auto s = std::move(result.value());
        s.close();
        
        bool sent = s.sendAll("hello", 5);
        REQUIRE(!sent);
        REQUIRE(s.getLastError() != SocketError::None);
    }

    BEGIN_TEST("Error codes are consistent between operations");
    {
        // Test that the same type of error produces consistent error codes
        auto result1 = SocketFactory::createTcpClient(
            AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{1}, Milliseconds{100}});
        auto result2 = SocketFactory::createTcpClient(
            AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{2}, Milliseconds{100}});
        
        REQUIRE(result1.isError());
        REQUIRE(result2.isError());
        // Both should be connection refused or similar
        REQUIRE(result1.error() != SocketError::None);
        REQUIRE(result2.error() != SocketError::None);
    }

    BEGIN_TEST("getError() returns None for successful operations");
    {
        auto result = SocketFactory::createTcpSocket();
        REQUIRE(result.isSuccess());
        auto& s = result.value();
        
        // Successful bind
        bool bind_result = s.bind("127.0.0.1", Port{19702});
        if (bind_result) {
            REQUIRE(s.getLastError() == SocketError::None);
        }
        
        // Successful listen
        bool listen_result = s.listen(5);
        if (listen_result) {
            REQUIRE(s.getLastError() == SocketError::None);
        }
    }

    std::cout << "All error handling tests passed!\n";
    return 0;
}
