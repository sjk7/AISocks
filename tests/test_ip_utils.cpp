// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
// Tests: IP address validation and conversion utilities.
// Checks observable behaviour only.

#include "Socket.h"
#include "test_helpers.h"
#include <cstdint>
#include <cstring>

using namespace aiSocks;

int main() {
    std::cout << "=== IP Utility Tests ===\n";

    // ---- isValidIPv4 ----
    BEGIN_TEST("isValidIPv4 - valid addresses");
    REQUIRE(Socket::isValidIPv4("127.0.0.1"));
    REQUIRE(Socket::isValidIPv4("0.0.0.0"));
    REQUIRE(Socket::isValidIPv4("255.255.255.255"));
    REQUIRE(Socket::isValidIPv4("192.168.1.100"));
    REQUIRE(Socket::isValidIPv4("10.0.0.1"));

    BEGIN_TEST("isValidIPv4 - invalid addresses");
    REQUIRE(!Socket::isValidIPv4("256.0.0.1"));
    REQUIRE(!Socket::isValidIPv4("192.168.1"));
    REQUIRE(!Socket::isValidIPv4(""));
    REQUIRE(!Socket::isValidIPv4("abc.def.ghi.jkl"));
    REQUIRE(!Socket::isValidIPv4("::1")); // IPv6, not v4
    REQUIRE(!Socket::isValidIPv4("1.2.3.4.5"));
    REQUIRE(!Socket::isValidIPv4("1.2.3.-1"));

    // ---- isValidIPv6 ----
    BEGIN_TEST("isValidIPv6 - valid addresses");
    REQUIRE(Socket::isValidIPv6("::1"));
    REQUIRE(Socket::isValidIPv6("::"));
    REQUIRE(Socket::isValidIPv6("fe80::1"));
    REQUIRE(Socket::isValidIPv6("2001:db8::1"));
    REQUIRE(Socket::isValidIPv6("2001:0db8:85a3:0000:0000:8a2e:0370:7334"));
    REQUIRE(Socket::isValidIPv6("::ffff:192.168.1.1")); // IPv4-mapped

    BEGIN_TEST("isValidIPv6 - invalid addresses");
    REQUIRE(!Socket::isValidIPv6(""));
    REQUIRE(!Socket::isValidIPv6("gggg::1"));
    REQUIRE(!Socket::isValidIPv6("127.0.0.1")); // IPv4, not v6
    REQUIRE(!Socket::isValidIPv6("not:an:address"));

    // ---- ipToString ----
    BEGIN_TEST("ipToString - IPv4 loopback 127.0.0.1");
    {
        // 127.0.0.1 in network byte order
        uint8_t addr[4] = {127, 0, 0, 1};
        std::string result = Socket::ipToString(addr, AddressFamily::IPv4);
        REQUIRE(result == "127.0.0.1");
    }

    BEGIN_TEST("ipToString - IPv4 all zeros");
    {
        uint8_t addr[4] = {0, 0, 0, 0};
        std::string result = Socket::ipToString(addr, AddressFamily::IPv4);
        REQUIRE(result == "0.0.0.0");
    }

    BEGIN_TEST("ipToString - IPv6 loopback ::1");
    {
        uint8_t addr[16] = {};
        addr[15] = 1;
        std::string result = Socket::ipToString(addr, AddressFamily::IPv6);
        REQUIRE(result == "::1");
    }

    // ---- getLocalAddresses ----
    BEGIN_TEST("getLocalAddresses returns at least one address");
    {
        auto ifaces = Socket::getLocalAddresses();
        REQUIRE(!ifaces.empty());
    }

    BEGIN_TEST(
        "getLocalAddresses - every entry has a non-empty address and name");
    {
        auto ifaces = Socket::getLocalAddresses();
        bool allValid = true;
        for (const auto& iface : ifaces) {
            if (iface.address.empty() || iface.name.empty()) {
                allValid = false;
            }
        }
        REQUIRE(allValid);
    }

    BEGIN_TEST("getLocalAddresses - loopback address present");
    {
        auto ifaces = Socket::getLocalAddresses();
        bool hasLoopback = false;
        for (const auto& iface : ifaces) {
            if (iface.isLoopback) {
                hasLoopback = true;
                break;
            }
        }
        REQUIRE(hasLoopback);
    }

    BEGIN_TEST(
        "getLocalAddresses - address family field matches address format");
    {
        auto ifaces = Socket::getLocalAddresses();
        bool allMatch = true;
        for (const auto& iface : ifaces) {
            if (iface.family == AddressFamily::IPv4
                && !Socket::isValidIPv4(iface.address))
                allMatch = false;
            if (iface.family == AddressFamily::IPv6
                && !Socket::isValidIPv6(iface.address))
                allMatch = false;
        }
        REQUIRE(allMatch);
    }

    return test_summary();
}
