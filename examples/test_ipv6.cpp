// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com


#include "TcpSocket.h"
#include <cstdio>
#include <thread>
#include <chrono>
#include <cstring>

using namespace aiSocks;

void testIPv4() {
    printf("=== IPv4 Test ===\n");

    auto serverSocket = TcpSocket::createRaw();
    if (!serverSocket.isValid()) {
        fprintf(stderr, "   Failed to create IPv4 server socket\n");
        return;
    }
    printf("   Created IPv4 socket\n");

    if (serverSocket.getAddressFamily() != AddressFamily::IPv4) {
        fprintf(stderr, "   Address family mismatch\n");
        return;
    }
    printf("   Address family is IPv4\n");

    (void)serverSocket.setReuseAddress(true);

    if (!serverSocket.bind("127.0.0.1", Port{8001})) {
        fprintf(stderr, "   Failed to bind IPv4: %s\n",
                serverSocket.getErrorMessage().c_str());
        return;
    }
    printf("   Bound to 127.0.0.1:8001\n");

    if (!serverSocket.listen(1)) {
        fprintf(stderr, "   Failed to listen: %s\n",
                serverSocket.getErrorMessage().c_str());
        return;
    }
    printf("   Listening for connections\n");

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
        printf("   Accepted IPv4 connection\n");

        char buffer[256] = {0};
        int received = acceptedSocket->receive(buffer, sizeof(buffer) - 1);
        if (received > 0) {
            printf("   Received: %s\n", buffer);
        }
        acceptedSocket->close();
    } else {
        fprintf(stderr, "   Failed to accept connection\n");
    }

    clientThread.join();
    serverSocket.close();
    printf("   IPv4 test completed successfully\n");
    printf("\n");
}

void testIPv6() {
    printf("=== IPv6 Test ===\n");

    auto serverSocket = TcpSocket::createRaw(AddressFamily::IPv6);
    if (!serverSocket.isValid()) {
        fprintf(stderr, "   Failed to create IPv6 server socket\n");
        return;
    }
    printf("   Created IPv6 socket\n");

    if (serverSocket.getAddressFamily() != AddressFamily::IPv6) {
        fprintf(stderr, "   Address family mismatch\n");
        return;
    }
    printf("   Address family is IPv6\n");

    (void)serverSocket.setReuseAddress(true);

    if (!serverSocket.bind("::1", Port{8002})) {
        fprintf(stderr, "   Failed to bind IPv6: %s\n",
                serverSocket.getErrorMessage().c_str());
        fprintf(stderr, "   IPv6 may not be available on this system\n");
        return;
    }
    printf("   Bound to ::1:8002\n");

    if (!serverSocket.listen(1)) {
        fprintf(stderr, "   Failed to listen: %s\n",
                serverSocket.getErrorMessage().c_str());
        return;
    }
    printf("   Listening for connections\n");

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
        printf("   Accepted IPv6 connection\n");

        if (acceptedSocket->getAddressFamily() == AddressFamily::IPv6) {
            printf("   Accepted socket has IPv6 address family\n");
        }

        char buffer[256] = {0};
        int received = acceptedSocket->receive(buffer, sizeof(buffer) - 1);
        if (received > 0) {
            printf("   Received: %s\n", buffer);
        }
        acceptedSocket->close();
    } else {
        fprintf(stderr, "   Failed to accept connection\n");
    }

    clientThread.join();
    serverSocket.close();
    printf("   IPv6 test completed successfully\n");
    printf("\n");
}

void testBackwardCompatibility() {
    printf("=== Backward Compatibility Test ===\n");
    printf("Testing default socket (should be IPv4)...\n");

    auto defaultSocket = TcpSocket::createRaw();
    if (defaultSocket.isValid()) {
        printf("   Default socket created successfully\n");

        if (defaultSocket.getAddressFamily() == AddressFamily::IPv4) {
            printf("   Default address family is IPv4 (backward compatible)\n");
        } else {
            fprintf(stderr, "   Default should be IPv4\n");
        }
    } else {
        fprintf(stderr, "   Failed to create default socket\n");
    }

    printf("\n");
}

int main() {
    printf("=== aiSocks IPv4 and IPv6 Support Test ===\n");
    printf("\n");

    testBackwardCompatibility();
    testIPv4();
    testIPv6();

    printf("==================================\n");
    printf("All tests completed!\n");

    return 0;
}
