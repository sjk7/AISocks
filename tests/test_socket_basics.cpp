// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
// Tests: Socket construction, validity, address family reporting.
// Checks observable behaviour only - no implementation details.

#include "TcpSocket.h"
#include "UdpSocket.h"
#include "test_helpers.h"

using namespace aiSocks;

int main() {
    std::cout << "=== Socket Construction Tests ===\n";

    BEGIN_TEST("TCP/IPv4 socket is valid after construction");
    {
        TcpSocket s;
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv4);
    }

    BEGIN_TEST("TCP/IPv6 socket is valid after construction");
    {
        TcpSocket s(AddressFamily::IPv6);
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv6);
    }

    BEGIN_TEST("UDP/IPv4 socket is valid after construction");
    {
        UdpSocket s;
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv4);
    }

    BEGIN_TEST("UDP/IPv6 socket is valid after construction");
    {
        UdpSocket s(AddressFamily::IPv6);
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv6);
    }

    BEGIN_TEST("Default constructor creates TCP/IPv4 socket");
    {
        TcpSocket s;
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv4);
    }

    BEGIN_TEST("Socket reports no error when freshly created");
    {
        TcpSocket s;
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("Socket is invalid after close()");
    {
        TcpSocket s;
        REQUIRE(s.isValid());
        s.close();
        REQUIRE(!s.isValid());
    }

    BEGIN_TEST("Calling close() twice does not crash");
    {
        TcpSocket s;
        s.close();
        s.close(); // must be safe
        REQUIRE_MSG(true, "double close() did not crash");
    }

    BEGIN_TEST("New socket is blocking by default");
    {
        TcpSocket s;
        REQUIRE(s.isBlocking());
    }

    return test_summary();
}
