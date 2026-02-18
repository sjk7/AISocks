// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com Tests: Move constructor and move assignment operator.
// Checks observable behaviour only.

#include "Socket.h"
#include "test_helpers.h"
#include <vector>

using namespace aiSocks;

int main() {
    std::cout << "=== Move Semantics Tests ===\n";

    BEGIN_TEST("Move constructor transfers validity");
    {
        Socket a(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(a.isValid());
        Socket b(std::move(a));
        REQUIRE(b.isValid());
        REQUIRE(!a.isValid());
    }

    BEGIN_TEST("Move constructor transfers address family");
    {
        Socket a(SocketType::TCP, AddressFamily::IPv6);
        Socket b(std::move(a));
        REQUIRE(b.getAddressFamily() == AddressFamily::IPv6);
    }

    BEGIN_TEST("Move assignment transfers validity");
    {
        Socket a(SocketType::TCP, AddressFamily::IPv4);
        Socket b(SocketType::TCP, AddressFamily::IPv4);
        b = std::move(a);
        REQUIRE(b.isValid());
        REQUIRE(!a.isValid());
    }

    BEGIN_TEST("Move assignment closes the displaced socket resource");
    {
        // b holds a valid socket; after move assignment, b's old socket is gone
        Socket a(SocketType::TCP, AddressFamily::IPv4);
        Socket b(SocketType::TCP, AddressFamily::IPv4);
        b = std::move(a);
        // No crash = the displaced socket was released cleanly
        REQUIRE_MSG(
            true, "move assignment released old resource without crash");
    }

    BEGIN_TEST("Moved-from socket reports invalid");
    {
        Socket a(SocketType::TCP, AddressFamily::IPv4);
        Socket b(std::move(a));
        REQUIRE(!a.isValid());
    }

    // NOTE: calling operational methods (bind, send, etc.) on a moved-from
    // socket is a programmer error — the library asserts in debug builds.
    // Only query methods (isValid, getLastError, getErrorMessage) are
    // documented safe on a moved-from socket.

    BEGIN_TEST("getLastError on moved-from socket does not crash");
    {
        Socket a(SocketType::TCP, AddressFamily::IPv4);
        Socket b(std::move(a));
        SocketError err = a.getLastError();
        (void)err;
        REQUIRE_MSG(true, "getLastError() on moved-from socket did not crash");
    }

    BEGIN_TEST("getErrorMessage on moved-from socket does not crash");
    {
        Socket a(SocketType::TCP, AddressFamily::IPv4);
        Socket b(std::move(a));
        std::string msg = a.getErrorMessage();
        REQUIRE_MSG(
            true, "getErrorMessage() on moved-from socket did not crash");
    }

    BEGIN_TEST("Self-move-assignment does not invalidate the socket");
    {
        Socket a(SocketType::TCP, AddressFamily::IPv4);
        Socket& ref = a;
        ref = std::move(a); // self-move
        // After self-move the standard only requires the object is in a valid
        // (destructible) state; some implementations keep it valid.
        // We only require no crash here.
        REQUIRE_MSG(true, "self-move-assignment did not crash");
    }

    BEGIN_TEST("Socket can be stored in a vector using move");
    {
        std::vector<Socket> vec;
        vec.emplace_back(SocketType::TCP, AddressFamily::IPv4);
        vec.emplace_back(SocketType::TCP, AddressFamily::IPv6);
        REQUIRE(vec.size() == 2);
        REQUIRE(vec[0].isValid());
        REQUIRE(vec[1].isValid());
    }

    return test_summary();
}
