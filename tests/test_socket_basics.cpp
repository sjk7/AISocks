// Tests: Socket construction, validity, address family reporting.
// Checks observable behaviour only - no implementation details.

#include "Socket.h"
#include "test_helpers.h"

using namespace aiSocks;

int main() {
    std::cout << "=== Socket Construction Tests ===\n";

    BEGIN_TEST("TCP/IPv4 socket is valid after construction");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv4);
    }

    BEGIN_TEST("TCP/IPv6 socket is valid after construction");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv6);
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv6);
    }

    BEGIN_TEST("UDP/IPv4 socket is valid after construction");
    {
        Socket s(SocketType::UDP, AddressFamily::IPv4);
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv4);
    }

    BEGIN_TEST("UDP/IPv6 socket is valid after construction");
    {
        Socket s(SocketType::UDP, AddressFamily::IPv6);
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv6);
    }

    BEGIN_TEST("Default constructor creates TCP/IPv4 socket");
    {
        Socket s;
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv4);
    }

    BEGIN_TEST("Socket reports no error when freshly created");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("Socket is invalid after close()");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(s.isValid());
        s.close();
        REQUIRE(!s.isValid());
    }

    BEGIN_TEST("Calling close() twice does not crash");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        s.close();
        s.close(); // must be safe
        REQUIRE_MSG(true, "double close() did not crash");
    }

    BEGIN_TEST("New socket is blocking by default");
    {
        Socket s(SocketType::TCP, AddressFamily::IPv4);
        REQUIRE(s.isBlocking());
    }

    return test_summary();
}
