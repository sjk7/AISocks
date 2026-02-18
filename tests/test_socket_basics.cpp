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
        auto s = TcpSocket::createRaw();
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv4);
    }

    BEGIN_TEST("TCP/IPv6 socket is valid after construction");
    {
        auto s = TcpSocket::createRaw(AddressFamily::IPv6);
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
        auto s = TcpSocket::createRaw();
        REQUIRE(s.isValid());
        REQUIRE(s.getAddressFamily() == AddressFamily::IPv4);
    }

    BEGIN_TEST("Socket reports no error when freshly created");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.getLastError() == SocketError::None);
    }

    BEGIN_TEST("Socket is invalid after close()");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.isValid());
        s.close();
        REQUIRE(!s.isValid());
    }

    BEGIN_TEST("Calling close() twice does not crash");
    {
        auto s = TcpSocket::createRaw();
        s.close();
        s.close(); // must be safe
        REQUIRE_MSG(true, "double close() did not crash");
    }

    BEGIN_TEST("New socket is blocking by default");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.isBlocking());
    }

    return test_summary();
}
