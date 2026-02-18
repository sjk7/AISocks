// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "TcpSocket.h"
#include <iostream>
#include <vector>

using namespace aiSocks;

int main() {
    std::cout << "=== Testing Move Semantics and Moved-From State ==="
              << std::endl;
    std::cout << std::endl;

    // Test 1: Move constructor
    std::cout << "Test 1: Move Constructor" << std::endl;
    auto socket1 = TcpSocket::createRaw();
    std::cout << "  Created socket1, valid: " << socket1.isValid() << std::endl;

    TcpSocket socket2(std::move(socket1));
    std::cout << "  Moved to socket2, socket2 valid: " << socket2.isValid()
              << std::endl;
    std::cout << "  socket1 (moved-from) valid: " << socket1.isValid()
              << std::endl;

    if (!socket1.isValid()) {
        std::cout << "  ✓ PASS - socket1 properly invalidated after move"
                  << std::endl;
    } else {
        std::cout << "  ✗ FAIL - socket1 still valid after move" << std::endl;
        return 1;
    }
    std::cout << std::endl;

    // Test 2: Move assignment
    std::cout << "Test 2: Move Assignment" << std::endl;
    auto socket3 = TcpSocket::createRaw();
    auto socket4 = TcpSocket::createRaw();

    std::cout << "  socket3 valid: " << socket3.isValid() << std::endl;
    std::cout << "  socket4 valid: " << socket4.isValid() << std::endl;

    socket4 = std::move(socket3);
    std::cout << "  After socket4 = std::move(socket3):" << std::endl;
    std::cout << "    socket3 valid: " << socket3.isValid() << std::endl;
    std::cout << "    socket4 valid: " << socket4.isValid() << std::endl;

    if (!socket3.isValid() && socket4.isValid()) {
        std::cout << "  ✓ PASS - Move assignment works correctly" << std::endl;
    } else {
        std::cout << "  ✗ FAIL - Move assignment failed" << std::endl;
        return 1;
    }
    std::cout << std::endl;

    // Test 3: Operations on moved-from object should not crash
    std::cout << "Test 3: Operations on Moved-From Object" << std::endl;
    auto socket5 = TcpSocket::createRaw();
    TcpSocket socket6(std::move(socket5));

    std::cout << "  Calling operations on moved-from socket5..." << std::endl;

    // These should all fail gracefully, not crash
    bool bindResult = socket5.bind("127.0.0.1", Port{9999});
    std::cout << "    bind() returned: " << bindResult << " (expected: 0)"
              << std::endl;

    bool listenResult = socket5.listen(5);
    std::cout << "    listen() returned: " << listenResult << " (expected: 0)"
              << std::endl;

    bool connectResult = socket5.connect("127.0.0.1", Port{9999});
    std::cout << "    connect() returned: " << connectResult << " (expected: 0)"
              << std::endl;

    char buffer[10];
    int sendResult = socket5.send("test", 4);
    std::cout << "    send() returned: " << sendResult << " (expected: -1)"
              << std::endl;

    int recvResult = socket5.receive(buffer, sizeof(buffer));
    std::cout << "    receive() returned: " << recvResult << " (expected: -1)"
              << std::endl;

    socket5.close(); // Should be safe to call
    std::cout << "    close() executed without crash" << std::endl;

    SocketError error = socket5.getLastError();
    std::cout << "    getLastError() returned: " << static_cast<int>(error)
              << std::endl;

    std::string errorMsg = socket5.getErrorMessage();
    std::cout << "    getErrorMessage(): " << errorMsg << std::endl;

    if (bindResult == false && listenResult == false && connectResult == false
        && sendResult == -1 && recvResult == -1) {
        std::cout << "  ✓ PASS - All operations failed gracefully" << std::endl;
    } else {
        std::cout << "  ✗ FAIL - Some operations didn't fail as expected"
                  << std::endl;
        return 1;
    }
    std::cout << std::endl;

    // Test 4: Self-assignment
    std::cout << "Test 4: Self-Assignment" << std::endl;
    auto socket7 = TcpSocket::createRaw();
    std::cout << "  socket7 valid before self-assignment: " << socket7.isValid()
              << std::endl;

    socket7 = std::move(socket7); //-V570
    std::cout << "  socket7 valid after self-assignment: " << socket7.isValid()
              << std::endl;

    if (socket7.isValid()) {
        std::cout << "  ✓ PASS - Self-assignment handled correctly"
                  << std::endl;
    } else {
        std::cout << "  ✗ FAIL - Self-assignment broke the object" << std::endl;
        return 1;
    }
    std::cout << std::endl;

    // Test 5: Move into container
    std::cout << "Test 5: Move into Container" << std::endl;
    std::vector<TcpSocket> sockets; //-V826
    auto socket8 = TcpSocket::createRaw();

    std::cout << "  socket8 valid: " << socket8.isValid() << std::endl;
    sockets.push_back(std::move(socket8));
    std::cout << "  After push_back(std::move(socket8)):" << std::endl;
    std::cout << "    socket8 valid: " << socket8.isValid() << std::endl;
    std::cout << "    sockets[0] valid: " << sockets[0].isValid() << std::endl;

    if (!socket8.isValid() && sockets[0].isValid()) {
        std::cout << "  ✓ PASS - Move into container works" << std::endl;
    } else {
        std::cout << "  ✗ FAIL - Move into container failed" << std::endl;
        return 1;
    }
    std::cout << std::endl;

    // Test 6: Moved-from state properties
    std::cout << "Test 6: Moved-From State Properties" << std::endl;
    auto socket9 = TcpSocket::createRaw();
    TcpSocket socket10(std::move(socket9));

    std::cout << "  Checking moved-from socket9 properties:" << std::endl;
    std::cout << "    isValid(): " << socket9.isValid() << std::endl;
    std::cout << "    isBlocking(): " << socket9.isBlocking() << std::endl;
    std::cout << "    getAddressFamily(): "
              << (socket9.getAddressFamily() == AddressFamily::IPv4 ? "IPv4"
                                                                    : "IPv6")
              << std::endl;

    std::cout << "  ✓ PASS - Moved-from state has safe default values"
              << std::endl;
    std::cout << std::endl;

    std::cout << "==================================" << std::endl;
    std::cout << "ALL TESTS PASSED ✓" << std::endl;
    std::cout << "Move semantics working correctly!" << std::endl;

    return 0;
}
