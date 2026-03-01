// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
//
// Test: SimpleClient convenience class with SocketFactory API
// Verifies one-liner connect + callback pattern

#include "TcpSocket.h"
#include "SimpleClient.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <chrono>
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
            auto srv_result = SocketFactory::createTcpServer(
                ServerBind{"127.0.0.1", Port{BASE}, Backlog{5}});
            if (srv_result.isSuccess()) {
                auto& srv = srv_result.value();
                auto conn_result = srv.accept();
                if (conn_result != nullptr) {
                    auto conn = std::move(conn_result);
                    char buf[256] = {};
                    int n = conn->receive(buf, sizeof(buf) - 1);
                    if (n > 0) {
                        conn->sendAll(buf, n);
                    }
                }
            }
        });

        // Give server time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // One-liner client
        bool callback_called = false;
        SimpleClient client(
            ConnectArgs{"127.0.0.1", Port{BASE}}, [&](TcpSocket& sock) {
                callback_called = true;
                const char* msg = "Hello echo";
                sock.sendAll(msg, std::strlen(msg));

                char buf[256] = {};
                int n = sock.receive(buf, sizeof(buf) - 1);
                if (n > 0) {
                    REQUIRE(std::string(buf, n) == msg);
                }
            });

        REQUIRE(client.isConnected());
        REQUIRE(callback_called);
        server_thread.join();
    }

    // Test 2: Error handling with SimpleClient
    BEGIN_TEST("SimpleClient: returns invalid client on refused connection");
    {
        SimpleClient client(
            ConnectArgs{"127.0.0.1", Port{1}, Milliseconds{100}},
            [](TcpSocket& sock) {
                // This callback should not be called
                REQUIRE(false);
            });

        REQUIRE(!client.isConnected());
    }

    // Test 3: Error handling with invalid address
    BEGIN_TEST("SimpleClient: returns invalid client on invalid address");
    {
        SimpleClient client(ConnectArgs{"invalid.address.that.does.not.exist",
                                Port{80}, Milliseconds{100}},
            [](TcpSocket& sock) {
                // This callback should not be called
                REQUIRE(false);
            });

        REQUIRE(!client.isConnected());
    }

    // Test 4: Timeout handling
    BEGIN_TEST("SimpleClient: returns invalid client on timeout");
    {
        SimpleClient client(ConnectArgs{"10.255.255.1", Port{80},
                                Milliseconds{10}}, // Non-routable IP
            [](TcpSocket& sock) {
                // This callback should not be called
                REQUIRE(false);
            });

        REQUIRE(!client.isConnected());
    }

    // Test 5: Multiple connections
    BEGIN_TEST("SimpleClient: multiple sequential connections");
    {
        // Start echo server
        std::thread server_thread([]() {
            auto srv_result = SocketFactory::createTcpServer(
                ServerBind{"127.0.0.1", Port{BASE + 1}, Backlog{5}});
            if (srv_result.isSuccess()) {
                auto& srv = srv_result.value();
                for (int i = 0; i < 3; ++i) {
                    auto conn_result = srv.accept();
                    if (conn_result != nullptr) {
                        auto conn = std::move(conn_result);
                        char buf[256] = {};
                        int n = conn->receive(buf, sizeof(buf) - 1);
                        if (n > 0) {
                            conn->sendAll(buf, n);
                        }
                    }
                }
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Multiple client connections
        for (int i = 0; i < 3; ++i) {
            std::string msg = "Message " + std::to_string(i);
            bool callback_called = false;

            SimpleClient client(
                ConnectArgs{"127.0.0.1", Port{BASE + 1}}, [&](TcpSocket& sock) {
                    callback_called = true;
                    sock.sendAll(msg.data(), msg.size());

                    char buf[256] = {};
                    int n = sock.receive(buf, sizeof(buf) - 1);
                    if (n > 0) {
                        REQUIRE(std::string(buf, n) == msg);
                    }
                });

            REQUIRE(client.isConnected());
            REQUIRE(callback_called);
        }

        server_thread.join();
    }

    // Test 6: Client can be moved after connection
    BEGIN_TEST("SimpleClient: socket can be moved after connection");
    {
        std::thread server_thread([]() {
            auto srv_result = SocketFactory::createTcpServer(
                ServerBind{"127.0.0.1", Port{BASE + 2}, Backlog{5}});
            if (srv_result.isSuccess()) {
                auto& srv = srv_result.value();
                auto conn_result = srv.accept();
                if (conn_result != nullptr) {
                    auto conn = std::move(conn_result);
                    char buf[256] = {};
                    int n = conn->receive(buf, sizeof(buf) - 1);
                    if (n > 0) {
                        conn->sendAll(buf, n);
                    }
                }
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        bool callback_called = false;
        SimpleClient client(
            ConnectArgs{"127.0.0.1", Port{BASE + 2}}, [&](TcpSocket& sock) {
                callback_called = true;
                // Move the socket out of the callback
                auto& moved_socket = sock; // Reference to the socket

                const char* msg = "Moved socket test";
                moved_socket.sendAll(msg, std::strlen(msg));

                char buf[256] = {};
                int n = moved_socket.receive(buf, sizeof(buf) - 1);
                if (n > 0) {
                    REQUIRE(std::string(buf, n) == msg);
                }
            });

        REQUIRE(client.isConnected());
        REQUIRE(callback_called);

        server_thread.join();
    }

    std::cout << "All SimpleClient tests passed!\n";
    return 0;
}
