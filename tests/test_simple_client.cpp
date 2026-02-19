// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Test: SimpleClient convenience class
// Verifies one-liner connect + callback pattern

#include "TcpSocket.h"
#include "SimpleClient.h"
#include "test_helpers.h"
#include <cstring>
#include <string>
#include <thread>

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
        try {
            SimpleClient client(ConnectArgs{"127.0.0.1", Port{BASE}}, [&](TcpSocket& sock) {
                const char* msg = "Hello echo";
                sock.sendAll(msg, std::strlen(msg));
                
                char buf[256] = {};
                int n = sock.receive(buf, sizeof(buf) - 1);
                callback_called = (n > 0 && std::string(buf, n) == "Hello echo");
            });

            REQUIRE(client.isConnected());
            REQUIRE(callback_called);
        } catch (const SocketException& e) {
            std::cerr << "  Exception: " << e.what() << "\n";
            REQUIRE(false);  // Connection should succeed
        }

        server_thread.join();
    }

    // Test 2: Connection failure handling (should throw)
    BEGIN_TEST("SimpleClient: throws on connection failure");
    {
        bool callback_called = false;
        bool exception_caught = false;

        try {
            SimpleClient client(ConnectArgs{"127.0.0.1", Port{65432}, Milliseconds{100}}, 
                [&](TcpSocket&) {
                callback_called = true; // Should not be called
            });
        } catch (const SocketException& e) {
            exception_caught = true;
        }

        REQUIRE_MSG(exception_caught,
            "SocketException thrown on connection failure");
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

        try {
            SimpleClient client(ConnectArgs{"127.0.0.1", Port{BASE + 1}}, [](TcpSocket& sock) {
                // Verify we can query the socket
                auto ep = sock.getPeerEndpoint();
                REQUIRE_MSG(ep.has_value(), "getPeerEndpoint returns valid endpoint");
            });

            REQUIRE(client.isConnected());
        } catch (const SocketException& e) {
            std::cerr << "  Exception: " << e.what() << "\n";
            REQUIRE(false);  // Connection should succeed
        }

        server_thread.join();
    }

    return test_summary();
}
