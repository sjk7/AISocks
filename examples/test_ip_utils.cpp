// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "TcpSocket.h"
#include <iostream>
#include <iomanip>

#ifdef SOMAXCONN
error("SOMAXCONN is defined, which may cause issues with our "
      "SocketImpl::listen() method. Compiler firewall broken!");

#endif

using namespace aiSocks;

int main() {
    std::cout << "=== aiSocks IP Address Utilities Test ===" << std::endl;
    std::cout << std::endl;

    // Test 1: Enumerate local IP addresses
    std::cout << "=== Local Network Interfaces ===" << std::endl;
    auto interfaces = Socket::getLocalAddresses();

    if (interfaces.empty()) {
        std::cout << "No network interfaces found!" << std::endl;
    } else {
        std::cout << "Found " << interfaces.size()
                  << " address(es):" << std::endl;
        std::cout << std::endl;

        for (const auto& iface : interfaces) {
            std::cout << "Interface: " << iface.name << std::endl;
            std::cout << "  Address:   " << iface.address << std::endl;
            std::cout << "  Family:    "
                      << (iface.family == AddressFamily::IPv4 ? "IPv4" : "IPv6")
                      << std::endl;
            std::cout << "  Loopback:  " << (iface.isLoopback ? "Yes" : "No")
                      << std::endl;
            std::cout << std::endl;
        }
    }

    // Test 2: Validate IPv4 addresses
    std::cout << "=== IPv4 Address Validation ===" << std::endl;
    std::vector<std::string> ipv4Tests = {
        //-V826
        "127.0.0.1", "192.168.1.1", "10.0.0.1",
        "256.256.256.256", // Invalid
        "192.168.1", // Invalid
        "abc.def.ghi.jkl" // Invalid
    };

    for (const auto& addr : ipv4Tests) {
        bool valid = Socket::isValidIPv4(addr);
        std::cout << "  " << std::setw(20) << std::left << addr
                  << (valid ? "✓ Valid" : "✗ Invalid") << std::endl;
    }
    std::cout << std::endl;

    // Test 3: Validate IPv6 addresses
    std::cout << "=== IPv6 Address Validation ===" << std::endl;
    std::vector<std::string> ipv6Tests = {
        //-V826
        "::1", "fe80::1", "2001:0db8:85a3:0000:0000:8a2e:0370:7334",
        "2001:db8::1",
        "::ffff:192.168.1.1", // IPv4-mapped IPv6
        "gggg::1" // Invalid
    };

    for (const auto& addr : ipv6Tests) {
        bool valid = Socket::isValidIPv6(addr);
        std::cout << "  " << std::setw(40) << std::left << addr
                  << (valid ? "✓ Valid" : "✗ Invalid") << std::endl;
    }
    std::cout << std::endl;

    // Test 4: IP to string conversion
    std::cout << "=== IP Address Conversion ===" << std::endl;

    // IPv4 conversion
    uint32_t ipv4Addr
        = 0x7F000001; // 127.0.0.1 in network byte order (big-endian)
    std::string ipv4Str = Socket::ipToString(&ipv4Addr, AddressFamily::IPv4);
    std::cout << "  IPv4 binary to string: " << ipv4Str << std::endl;

    // IPv6 conversion (::1)
    uint8_t ipv6Addr[16] = {0};
    ipv6Addr[15] = 1; // ::1
    std::string ipv6Str = Socket::ipToString(ipv6Addr, AddressFamily::IPv6);
    std::cout << "  IPv6 binary to string: " << ipv6Str << std::endl;
    std::cout << std::endl;

    // Test 5: Find non-loopback addresses
    std::cout << "=== Non-Loopback Addresses ===" << std::endl;
    bool foundNonLoopback = false;

    for (const auto& iface : interfaces) {
        if (!iface.isLoopback) {
            std::cout << "  " << iface.address << " ("
                      << (iface.family == AddressFamily::IPv4 ? "IPv4" : "IPv6")
                      << ") on " << iface.name << std::endl;
            foundNonLoopback = true;
        }
    }

    if (!foundNonLoopback) {
        std::cout << "  No non-loopback addresses found" << std::endl;
    }
    std::cout << std::endl;

    std::cout << "==================================" << std::endl;
    std::cout << "IP utilities test completed!" << std::endl;

    return 0;
}
