// This is a personal academic project. Dear PVS-Studio, please check it.
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
    std::cout << "=== Blocking State Tests ===\n";

    BEGIN_TEST("New socket is blocking by default");
    {
        TcpSocket s;
        REQUIRE(s.isBlocking());
    }

    BEGIN_TEST("setBlocking(false) returns true and makes socket non-blocking");
    {
        TcpSocket s;
        REQUIRE(s.setBlocking(false));
        REQUIRE(!s.isBlocking());
    }

    BEGIN_TEST("setBlocking(true) restores blocking mode");
    {
        TcpSocket s;
        s.setBlocking(false);
        REQUIRE(s.setBlocking(true));
        REQUIRE(s.isBlocking());
    }

    BEGIN_TEST("Blocking mode can be toggled multiple times correctly");
    {
        TcpSocket s;
        bool ok = true;
        for (int i = 0; i < 6; ++i) {
            bool target = (i % 2 == 0) ? false : true;
            s.setBlocking(target);
            if (s.isBlocking() != target) {
                ok = false;
                break;
            }
        }
        REQUIRE(ok);
    }

    BEGIN_TEST("UDP socket blocking mode behaves the same as TCP");
    {
        UdpSocket s;
        REQUIRE(s.isBlocking());
        REQUIRE(s.setBlocking(false));
        REQUIRE(!s.isBlocking());
        REQUIRE(s.setBlocking(true));
        REQUIRE(s.isBlocking());
    }

    BEGIN_TEST("Non-blocking recv on unconnected socket returns WouldBlock or "
               "error instantly");
    {
        TcpSocket s;
        s.setBlocking(false);
        char buf[64];
        int r = s.receive(buf, sizeof(buf));
        // Must return quickly (non-blocking) - either WouldBlock or an error
        bool quickReturn = (r < 0);
        REQUIRE(quickReturn);
    }

    BEGIN_TEST(
        "Accepted socket inherits blocking state (defaults to blocking)");
    {
        TcpSocket server;
        server.setReuseAddress(true);
        // Use a fixed port; if in use the test is skipped gracefully
        bool bound = server.bind("127.0.0.1", Port{19300}) && server.listen(1);
        if (!bound) {
            REQUIRE_MSG(true, "SKIP - port 19300 unavailable");
        } else {
            std::thread connector([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                TcpSocket c;
                c.connect("127.0.0.1", Port{19300});
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            });
            auto accepted = server.accept();
            connector.join();
            REQUIRE(accepted != nullptr);
            REQUIRE(accepted->isBlocking());
        }
    }

    return test_summary();
}
