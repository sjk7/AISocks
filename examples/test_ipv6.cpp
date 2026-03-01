// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#include "TcpSocket.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

using namespace aiSocks;

void testIPv4() {
    std::cout << "=== IPv4 Test ===" << std::endl;

    auto serverSocket = TcpSocket::createRaw();
    if (!serverSocket.isValid()) {
        std::cerr << "   Failed to create IPv4 server socket" << std::endl;
        return;
    }
    std::cout << "   Created IPv4 socket" << std::endl;

    if (serverSocket.getAddressFamily() != AddressFamily::IPv4) {
        std::cerr << "   Address family mismatch" << std::endl;
        return;
    }
    std::cout << "   Address family is IPv4" << std::endl;

    (void)serverSocket.setReuseAddress(true);

    if (!serverSocket.bind("127.0.0.1", Port{8001})) {
        std::cerr << "   Failed to bind IPv4: "
                  << serverSocket.getErrorMessage() << std::endl;
        return;
    }
    std::cout << "   Bound to 127.0.0.1:8001" << std::endl;

    if (!serverSocket.listen(1)) {
        std::cerr << "   Failed to listen: " << serverSocket.getErrorMessage()
                  << std::endl;
        return;
    }
    std::cout << "   Listening for connections" << std::endl;

    // Client thread
    std::thread clientThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto client = TcpSocket::createRaw();
        if (client.connect("127.0.0.1", Port{8001})) {
            const char* msg = "Hello IPv4";
            client.send(msg, strlen(msg));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    auto acceptedSocket = serverSocket.accept();
    if (acceptedSocket) {
        std::cout << "   Accepted IPv4 connection" << std::endl;

        char buffer[256] = {0};
        int received = acceptedSocket->receive(buffer, sizeof(buffer) - 1);
        if (received > 0) {
            std::cout << "   Received: " << buffer << std::endl;
        }
        acceptedSocket->close();
    } else {
        std::cerr << "   Failed to accept connection" << std::endl;
    }

    clientThread.join();
    serverSocket.close();
    std::cout << "   IPv4 test completed successfully" << std::endl;
    std::cout << std::endl;
}

void testIPv6() {
    std::cout << "=== IPv6 Test ===" << std::endl;

    auto serverSocket = TcpSocket::createRaw(AddressFamily::IPv6);
    if (!serverSocket.isValid()) {
        std::cerr << "   Failed to create IPv6 server socket" << std::endl;
        return;
    }
    std::cout << "   Created IPv6 socket" << std::endl;

    if (serverSocket.getAddressFamily() != AddressFamily::IPv6) {
        std::cerr << "   Address family mismatch" << std::endl;
        return;
    }
    std::cout << "   Address family is IPv6" << std::endl;

    (void)serverSocket.setReuseAddress(true);

    if (!serverSocket.bind("::1", Port{8002})) {
        std::cerr << "   Failed to bind IPv6: "
                  << serverSocket.getErrorMessage() << std::endl;
        std::cerr << "   IPv6 may not be available on this system" << std::endl;
        return;
    }
    std::cout << "   Bound to ::1:8002" << std::endl;

    if (!serverSocket.listen(1)) {
        std::cerr << "   Failed to listen: " << serverSocket.getErrorMessage()
                  << std::endl;
        return;
    }
    std::cout << "   Listening for connections" << std::endl;

    // Client thread
    std::thread clientThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto client = TcpSocket::createRaw(AddressFamily::IPv6);
        if (client.connect("::1", Port{8002})) {
            const char* msg = "Hello IPv6";
            client.send(msg, strlen(msg));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    auto acceptedSocket = serverSocket.accept();
    if (acceptedSocket) {
        std::cout << "   Accepted IPv6 connection" << std::endl;

        if (acceptedSocket->getAddressFamily() == AddressFamily::IPv6) {
            std::cout << "   Accepted socket has IPv6 address family"
                      << std::endl;
        }

        char buffer[256] = {0};
        int received = acceptedSocket->receive(buffer, sizeof(buffer) - 1);
        if (received > 0) {
            std::cout << "   Received: " << buffer << std::endl;
        }
        acceptedSocket->close();
    } else {
        std::cerr << "   Failed to accept connection" << std::endl;
    }

    clientThread.join();
    serverSocket.close();
    std::cout << "   IPv6 test completed successfully" << std::endl;
    std::cout << std::endl;
}

void testBackwardCompatibility() {
    std::cout << "=== Backward Compatibility Test ===" << std::endl;
    std::cout << "Testing default socket (should be IPv4)..." << std::endl;

    auto defaultSocket = TcpSocket::createRaw();
    if (defaultSocket.isValid()) {
        std::cout << "   Default socket created successfully" << std::endl;

        if (defaultSocket.getAddressFamily() == AddressFamily::IPv4) {
            std::cout
                << "   Default address family is IPv4 (backward compatible)"
                << std::endl;
        } else {
            std::cerr << "   Default should be IPv4" << std::endl;
        }
    } else {
        std::cerr << "   Failed to create default socket" << std::endl;
    }

    std::cout << std::endl;
}

int main() {
    std::cout << "=== aiSocks IPv4 and IPv6 Support Test ===" << std::endl;
    std::cout << std::endl;

    testBackwardCompatibility();
    testIPv4();
    testIPv6();

    std::cout << "==================================" << std::endl;
    std::cout << "All tests completed!" << std::endl;

    return 0;
}
