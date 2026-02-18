// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "TcpSocket.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace aiSocks;

int main() {
    std::cout << "=== Testing Blocking State Tracking ===" << std::endl;
    std::cout << std::endl;

    // Create a TCP socket
    auto socket = TcpSocket::createRaw();

    if (!socket.isValid()) {
        std::cerr << "Failed to create socket: " << socket.getErrorMessage()
                  << std::endl;
        return 1;
    }

    // Test 1: Default state should be blocking
    std::cout << "Test 1: Default blocking state" << std::endl;
    bool defaultBlocking = socket.isBlocking();
    std::cout << "  Socket is blocking: " << (defaultBlocking ? "YES" : "NO")
              << std::endl;
    if (defaultBlocking) {
        std::cout << "  ✓ PASS - Default state is blocking" << std::endl;
    } else {
        std::cout << "  ✗ FAIL - Expected blocking=true by default"
                  << std::endl;
        return 1;
    }
    std::cout << std::endl;

    // Test 2: Set to non-blocking
    std::cout << "Test 2: Set to non-blocking mode" << std::endl;
    if (!socket.setBlocking(false)) {
        std::cerr << "  ✗ FAIL - Could not set non-blocking mode: "
                  << socket.getErrorMessage() << std::endl;
        return 1;
    }
    bool isNonBlocking = !socket.isBlocking();
    std::cout << "  Socket is non-blocking: " << (isNonBlocking ? "YES" : "NO")
              << std::endl;
    if (isNonBlocking) {
        std::cout << "  ✓ PASS - Successfully set to non-blocking" << std::endl;
    } else {
        std::cout << "  ✗ FAIL - State not updated after setBlocking(false)"
                  << std::endl;
        return 1;
    }
    std::cout << std::endl;

    // Test 3: Set back to blocking
    std::cout << "Test 3: Set back to blocking mode" << std::endl;
    if (!socket.setBlocking(true)) {
        std::cerr << "  ✗ FAIL - Could not set blocking mode: "
                  << socket.getErrorMessage() << std::endl;
        return 1;
    }
    bool isBlockingAgain = socket.isBlocking();
    std::cout << "  Socket is blocking: " << (isBlockingAgain ? "YES" : "NO")
              << std::endl;
    if (isBlockingAgain) {
        std::cout << "  ✓ PASS - Successfully set back to blocking"
                  << std::endl;
    } else {
        std::cout << "  ✗ FAIL - State not updated after setBlocking(true)"
                  << std::endl;
        return 1;
    }
    std::cout << std::endl;

    // Test 4: Multiple toggles
    std::cout << "Test 4: Multiple toggles" << std::endl;
    bool allPassed = true;
    for (int i = 0; i < 5; i++) {
        bool targetState = (i % 2 == 0);
        (void)socket.setBlocking(targetState);
        bool currentState = socket.isBlocking();
        std::cout << "  Toggle " << (i + 1) << ": Expected="
                  << (targetState ? "blocking" : "non-blocking") << ", Actual="
                  << (currentState ? "blocking" : "non-blocking");
        if (currentState == targetState) {
            std::cout << " ✓" << std::endl;
        } else {
            std::cout << " ✗" << std::endl;
            allPassed = false;
        }
    }
    if (allPassed) {
        std::cout << "  ✓ PASS - All toggles tracked correctly" << std::endl;
    } else {
        std::cout << "  ✗ FAIL - Some toggles not tracked correctly"
                  << std::endl;
        return 1;
    }
    std::cout << std::endl;

    // Test 5: Accepted socket should default to blocking
    std::cout << "Test 5: Accepted socket blocking state" << std::endl;
    auto serverSocket = TcpSocket::createRaw();
    serverSocket.setReuseAddress(true);

    if (serverSocket.bind("127.0.0.1", Port{9999}) && serverSocket.listen(1)) {
        std::cout << "  Server bound to port 9999" << std::endl;

        // Create client socket and connect in a separate thread
        std::thread clientThread([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto client = TcpSocket::createRaw();
            (void)client.connect("127.0.0.1", Port{9999});
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        });

        // Accept connection
        auto acceptedSocket = serverSocket.accept();
        if (acceptedSocket) {
            bool acceptedIsBlocking = acceptedSocket->isBlocking();
            std::cout << "  Accepted socket is blocking: "
                      << (acceptedIsBlocking ? "YES" : "NO") << std::endl;
            if (acceptedIsBlocking) {
                std::cout << "  ✓ PASS - Accepted socket defaults to blocking"
                          << std::endl;
            } else {
                std::cout
                    << "  ✗ FAIL - Accepted socket should default to blocking"
                    << std::endl;
                clientThread.join();
                return 1;
            }
            acceptedSocket->close();
        } else {
            std::cout << "  ✗ FAIL - Could not accept connection" << std::endl;
            clientThread.join();
            return 1;
        }

        clientThread.join();
        serverSocket.close();
    } else {
        std::cout << "  ⚠ SKIP - Could not bind/listen on port 9999"
                  << std::endl;
    }
    std::cout << std::endl;

    socket.close();

    std::cout << "==================================" << std::endl;
    std::cout << "ALL TESTS PASSED ✓" << std::endl;
    std::cout << "Blocking state tracking is working correctly!" << std::endl;

    return 0;
}
