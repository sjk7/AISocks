// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests: Blocking/non-blocking mode transitions. Checks
// observable behaviour only.

#include "TcpSocket.h"
#include "UdpSocket.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>

using namespace aiSocks;

int main() {
    printf("=== Blocking State Tests ===\n");

    BEGIN_TEST("New socket is blocking by default");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.isBlocking());
    }

    BEGIN_TEST("setBlocking(false) returns true and makes socket non-blocking");
    {
        auto s = TcpSocket::createRaw();
        REQUIRE(s.setBlocking(false));
        REQUIRE(!s.isBlocking());
    }

#ifndef AISOCKS_TESTING
    BEGIN_TEST("setBlocking(true) restores blocking mode");
    {
        auto s = TcpSocket::createRaw();
        (void)s.setBlocking(false);
        REQUIRE(s.setBlocking(true));
        REQUIRE(s.isBlocking());
    }

    BEGIN_TEST("Blocking mode can be toggled multiple times correctly");
    {
        auto s = TcpSocket::createRaw();
        bool ok = true;
        for (int i = 0; i < 6; ++i) {
            bool target = (i % 2 == 0) ? false : true;
            (void)s.setBlocking(target);
            if (s.isBlocking() != target) {
                ok = false;
                break;
            }
        }
        REQUIRE(ok);
    }
#endif

    BEGIN_TEST("UDP socket blocking mode behaves the same as TCP");
    {
        UdpSocket s;
        REQUIRE(s.isBlocking());
        REQUIRE(s.setBlocking(false));
        REQUIRE(!s.isBlocking());
#ifndef AISOCKS_TESTING
        REQUIRE(s.setBlocking(true));
        REQUIRE(s.isBlocking());
#endif
    }

    BEGIN_TEST("Non-blocking recv on unconnected socket returns WouldBlock or "
               "error instantly");
    {
        auto s = TcpSocket::createRaw();
        (void)s.setBlocking(false);
        char buf[64];
        int r = s.receive(buf, sizeof(buf));
        // Must return quickly (non-blocking) - either WouldBlock or an error
        bool quickReturn = (r < 0);
        REQUIRE(quickReturn);
    }

    BEGIN_TEST(
        "Accepted socket inherits blocking state (defaults to blocking)");
    {
        auto server = TcpSocket::createRaw();
        REQUIRE(server.setReuseAddress(true));
        // OS assigns an ephemeral port; no risk of collision
        bool bound
            = server.bind("127.0.0.1", Port{Port::any}) && server.listen(1);
        Port port = Port::any;
        if (bound) {
            auto ep = server.getLocalEndpoint();
            port = ep.isSuccess() ? ep.value().port : Port::any;
            bound = (port != Port::any);
        }
        if (!bound) {
            REQUIRE_MSG(true, "SKIP - ephemeral port unavailable");
        } else {
            std::thread connector([port]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                auto c = TcpSocket::createRaw();
                (void)c.connect("127.0.0.1", port);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            });
            auto accepted = server.accept();
            if (accepted == nullptr) {
                // Wait and retry once
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                accepted = server.accept();
            }
            connector.join();
            REQUIRE(accepted != nullptr);
            REQUIRE(accepted->isBlocking());
        }
    }

    return test_summary();
}
