// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Test: SimpleClient convenience class
// Verifies one-liner connect + callback pattern

#include "TcpSocket.h"
#include "SimpleClient.h"
#include "test_helpers.h"
#include <thread>
#include <string>

using namespace aiSocks;

static constexpr uint16_t BASE = 22000;

int main() {
    std::cout << "=== SimpleClient Tests ===\n";

    // Test 1: One-liner connect with callback
    BEGIN_TEST("SimpleClient: one-liner connect + echo callback");
    {
        // Start echo server in background
        std::thread server_thread([]() {
            TcpSocket srv(AddressFamily::IPv4,
                ServerBind{"127.0.0.1", Port{BASE}, 5, false});
            auto conn = srv.accept();
            if (conn) {
                char buf[256] = {};
                int n = conn->receive(buf, sizeof(buf) - 1);
                if (n > 0) {
                    conn->sendAll(buf, n);
                }
            }
        });

        // Give server time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // One-liner client
        bool callback_called = false;
        SimpleClient client("127.0.0.1", Port{BASE}, [&](TcpSocket& sock) {
            const char* msg = "Hello echo";
            sock.sendAll(msg, std::strlen(msg));
            
            char buf[256] = {};
            int n = sock.receive(buf, sizeof(buf) - 1);
            callback_called = (n > 0 && std::string(buf, n) == "Hello echo");
        });

        server_thread.join();

        REQUIRE(client.isConnected());
        REQUIRE(callback_called);
    }

    // Test 2: Connection failure handling
    BEGIN_TEST("SimpleClient: detects connection failure");
    {
        bool callback_called = false;
        SimpleClient client("127.0.0.1", Port{65432}, [&](TcpSocket&) {
            callback_called = true; // Should not be called
        }, Milliseconds{100});

        REQUIRE_MSG(!client.isConnected(),
            "isConnected() returns false on failed connection");
        REQUIRE_MSG(!callback_called,
            "callback not invoked on connection failure");
    }

    // Test 3: Socket access after successful connect
    BEGIN_TEST("SimpleClient: getSocket() provides socket access");
    {
        std::thread server_thread([]() {
            TcpSocket srv(AddressFamily::IPv4,
                ServerBind{"127.0.0.1", Port{BASE + 1}, 5, false});
            auto conn = srv.accept();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        SimpleClient client("127.0.0.1", Port{BASE + 1}, [](TcpSocket& sock) {
            // Verify we can query the socket
            auto ep = sock.getPeerEndpoint();
            REQUIRE_MSG(ep.has_value(), "getPeerEndpoint returns valid endpoint");
        });

        server_thread.join();
        REQUIRE(client.isConnected());
    }

    return test_summary();
}
