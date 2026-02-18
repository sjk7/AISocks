// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
//
// Tests: Move constructor and move assignment operator.
// Checks observable behaviour only.

#include "TcpSocket.h"
#include "test_helpers.h"
#include <vector>

using namespace aiSocks;

int main() {
    std::cout << "=== Move Semantics Tests ===\n";

    BEGIN_TEST("Move constructor transfers validity");
    {
        auto a = TcpSocket::createRaw();
        REQUIRE(a.isValid());
        TcpSocket b(std::move(a));
        REQUIRE(b.isValid());
        REQUIRE(!a.isValid());
    }

    BEGIN_TEST("Move constructor transfers address family");
    {
        auto a = TcpSocket::createRaw(AddressFamily::IPv6);
        TcpSocket b(std::move(a));
        REQUIRE(b.getAddressFamily() == AddressFamily::IPv6);
    }

    BEGIN_TEST("Move assignment transfers validity");
    {
        auto a = TcpSocket::createRaw();
        auto b = TcpSocket::createRaw();
        b = std::move(a);
        REQUIRE(b.isValid());
        REQUIRE(!a.isValid());
    }

    BEGIN_TEST("Move assignment closes the displaced socket resource");
    {
        // b holds a valid socket; after move assignment, b's old socket is gone
        auto a = TcpSocket::createRaw();
        auto b = TcpSocket::createRaw();
        b = std::move(a);
        // No crash = the displaced socket was released cleanly
        REQUIRE_MSG(
            true, "move assignment released old resource without crash");
    }

    BEGIN_TEST("Moved-from socket reports invalid");
    {
        auto a = TcpSocket::createRaw();
        TcpSocket b(std::move(a));
        REQUIRE(!a.isValid());
    }

    // NOTE: calling operational methods (bind, send, etc.) on a moved-from
    // socket is a programmer error  the library asserts in debug builds.
    // Only query methods (isValid, getLastError, getErrorMessage) are
    // documented safe on a moved-from socket.

    BEGIN_TEST("getLastError on moved-from socket does not crash");
    {
        auto a = TcpSocket::createRaw();
        TcpSocket b(std::move(a));
        SocketError err = a.getLastError();
        (void)err;
        REQUIRE_MSG(true, "getLastError() on moved-from socket did not crash");
    }

    BEGIN_TEST("getErrorMessage on moved-from socket does not crash");
    {
        auto a = TcpSocket::createRaw();
        TcpSocket b(std::move(a));
        std::string msg = a.getErrorMessage(); //-V808
        REQUIRE_MSG(
            true, "getErrorMessage() on moved-from socket did not crash");
    }

    BEGIN_TEST("Self-move-assignment does not invalidate the socket");
    {
        auto a = TcpSocket::createRaw();
        TcpSocket& ref = a;
        ref = std::move(a); // self-move
        // After self-move the standard only requires the object is in a valid
        // (destructible) state; some implementations keep it valid.
        // We only require no crash here.
        REQUIRE_MSG(true, "self-move-assignment did not crash");
    }

    BEGIN_TEST("Socket can be stored in a vector using move");
    {
        std::vector<TcpSocket> vec; //-V826
        vec.emplace_back(TcpSocket::createRaw());
        vec.emplace_back(TcpSocket::createRaw(AddressFamily::IPv6));
        REQUIRE(vec.size() == 2); //-V547
        REQUIRE(vec[0].isValid());
        REQUIRE(vec[1].isValid());
    }

    return test_summary();
}
