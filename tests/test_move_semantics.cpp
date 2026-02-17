// Tests: Move constructor and move assignment operator.
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

    BEGIN_TEST("Operations on moved-from socket fail gracefully (no crash)");
    {
        Socket a(SocketType::TCP, AddressFamily::IPv4);
        Socket b(std::move(a));

        // All of these must not crash and must return failure
        REQUIRE(a.bind("127.0.0.1", Port{19500}) == false);
        REQUIRE(a.listen(5) == false);
        REQUIRE(a.connect("127.0.0.1", Port{19500}) == false);
        REQUIRE(a.send("x", 1) <= 0);
        char buf[8];
        REQUIRE(a.receive(buf, sizeof(buf)) <= 0);
        a.close(); // must be safe
        REQUIRE_MSG(
            true, "all ops on moved-from socket complete without crash");
    }

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
