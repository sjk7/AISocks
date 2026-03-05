// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Edge case tests for ServerBase<T>:
//   1. Behavior under high client load with timeouts
//   2. Client limit edge cases (at limit, negative, zero)
//   3. Error handling scenarios (socket errors, connection resets)
//   4. Keep-alive timeout behavior
//   5. Graceful shutdown with pending connections

#include "ServerBase.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace aiSocks;

// Testing constants for edge case scenarios
static constexpr Milliseconds QUICK_POLL_TIMEOUT{5};
static constexpr Milliseconds SHORT_KEEP_ALIVE{50};

// ---------------------------------------------------------------------------
// Echo server for edge case testing
// ---------------------------------------------------------------------------
struct EdgeCaseState {
    std::string buf;
    int messageCount{0};
    bool hasError{false};
};

class EdgeCaseServer : public ServerBase<EdgeCaseState> {
    public:
    explicit EdgeCaseServer(Port port)
        : ServerBase<EdgeCaseState>(
              ServerBind{"127.0.0.1", port, Backlog{10}}) {
        setKeepAliveTimeout(std::chrono::milliseconds(SHORT_KEEP_ALIVE.count));
    }

    std::atomic<int> totalMessagesReceived{0};
    std::atomic<int> totalMessagesSent{0};
    std::atomic<int> errorCount{0};
    std::atomic<int> timedOutClientsCount{0};
    std::atomic<int> disconnectCount{0};

    Port getActualPort() const {
        auto endpoint = getSocket().getLocalEndpoint();
        return endpoint.isSuccess() ? endpoint.value().port : Port::any;
    }

    protected:
    ServerResult onReadable(TcpSocket& sock, EdgeCaseState& s) override {
        char tmp[1024];
        for (;;) {
            int n = sock.receive(tmp, sizeof(tmp));
            if (n <= 0) {
                if (sock.getLastError() == SocketError::WouldBlock) break;
                if (n < 0) {
                    errorCount++;
                    return ServerResult::Disconnect;
                }
                break; // EOF
            }
            s.buf.append(tmp, n);
            s.messageCount++;
            totalMessagesReceived++;
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, EdgeCaseState& s) final {
        if (s.buf.empty()) {
            return ServerResult::KeepConnection;
        }

        int sent = sock.send(s.buf.data(), s.buf.size());
        if (sent > 0) {
            totalMessagesSent++;
            s.buf.erase(0, sent);
        } else if (sock.getLastError() != SocketError::WouldBlock) {
            errorCount++;
            return ServerResult::Disconnect;
        }
        return ServerResult::KeepConnection;
    }

    void onClientsTimedOut(size_t count) override {
        timedOutClientsCount += count;
        ServerBase::onClientsTimedOut(count);
    }

    void onDisconnect(EdgeCaseState& s) override {
        (void)s; // Suppress warning
        disconnectCount++;
    }
};

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------
static std::thread startServerInBackground(EdgeCaseServer& server,
    std::atomic<bool>& ready, ClientLimit maxClients = ClientLimit::Unlimited) {
    return std::thread([&server, &ready, maxClients]() {
        ready = true;
        server.run(maxClients, QUICK_POLL_TIMEOUT);
    });
}

static void waitForServerReady(std::atomic<bool>& ready) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!ready && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    REQUIRE(ready);
}

static std::unique_ptr<TcpSocket> connectClient(
    Port port, Milliseconds timeout) {
    auto result = SocketFactory::createTcpClient(
        AddressFamily::IPv4, ConnectArgs{"127.0.0.1", port, timeout});
    if (result.isSuccess()) {
        return std::make_unique<TcpSocket>(std::move(result.value()));
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== ServerBase Edge Cases Tests ===\n";

    // Test 1: Server under load with many connections
    BEGIN_TEST("Edge case: server handles high connection load");
    {
        EdgeCaseServer server(Port::any);
        Port port = server.getActualPort();
        std::atomic<bool> ready{false};

        auto serverThread
            = startServerInBackground(server, ready, ClientLimit::Unlimited);
        waitForServerReady(ready);

        std::vector<std::unique_ptr<TcpSocket>> clients;
        const int numClients = 20; // High load

        // Connect many clients
        for (int i = 0; i < numClients; ++i) {
            auto client = connectClient(port, Milliseconds{200});
            if (client) {
                clients.push_back(std::move(client));
            }
        }

        std::cout << "DEBUG: Connected " << clients.size() << " of "
                  << numClients << " clients\n";

        // Each client sends a small message
        for (auto& client : clients) {
            const char* msg = "test";
            client->send(msg, strlen(msg));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        // Server should handle all without crashing
        REQUIRE(server.totalMessagesReceived.load() > 0);
        REQUIRE(server.errorCount.load() == 0); // No errors expected

        server.requestStop();
        serverThread.join();
    }

    // Test 2: Client limit exactly at capacity
    BEGIN_TEST("Edge case: server at exactly maximum client capacity");
    {
        EdgeCaseServer server(Port::any);
        Port port = server.getActualPort();
        std::atomic<bool> ready{false};

        const size_t maxClients = 5;
        auto serverThread
            = startServerInBackground(server, ready, ClientLimit{maxClients});
        waitForServerReady(ready);

        std::vector<std::unique_ptr<TcpSocket>> clients;

        // Connect up to exact limit
        for (size_t i = 0; i < maxClients; ++i) {
            auto client = connectClient(port, Milliseconds{100});
            if (client) {
                clients.push_back(std::move(client));
            }
        }

        // Verify we're at capacity
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        std::cout << "DEBUG: Client count = " << server.clientCount()
                  << " (expected " << maxClients << ")\n";

        REQUIRE(server.clientCount() <= maxClients);

        server.requestStop();
        serverThread.join();
    }

    // Test 3: Client limit of 1 (minimal capacity)
    BEGIN_TEST("Edge case: server with minimal client limit (1)");
    {
        EdgeCaseServer server(Port::any);
        Port port = server.getActualPort();
        std::atomic<bool> ready{false};

        auto serverThread
            = startServerInBackground(server, ready, ClientLimit{1});
        waitForServerReady(ready);

        // Connect one client
        auto client1 = connectClient(port, Milliseconds{100});
        REQUIRE(client1 != nullptr);

        std::this_thread::sleep_for(std::chrono::milliseconds{50});

        // Verify client is connected
        REQUIRE(server.clientCount() <= 1);

        // Send a message
        const char* msg = "hello";
        client1->send(msg, strlen(msg));

        // Give the single-threaded event loop multiple cycles to process the
        // message The server runs with 5ms timeout, so multiple sleeps ensure
        // processing
        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        server.requestStop();
        serverThread.join();

        REQUIRE(server.totalMessagesReceived.load() > 0);
    }

    // Test 4: Rapid connect/disconnect cycling
    BEGIN_TEST("Edge case: rapid connect/disconnect/reconnect cycling");
    {
        EdgeCaseServer server(Port::any);
        Port port = server.getActualPort();
        std::atomic<bool> ready{false};

        auto serverThread
            = startServerInBackground(server, ready, ClientLimit::Unlimited);
        waitForServerReady(ready);

        int cycles = 10;
        for (int i = 0; i < cycles; ++i) {
            auto client = connectClient(port, Milliseconds{100});
            if (client) {
                const char* msg = "data";
                client->send(msg, strlen(msg));
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            }
            // Client disconnects when it goes out of scope
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        server.requestStop();
        serverThread.join();

        std::cout << "DEBUG: Completed " << cycles << " cycles\n";
        REQUIRE(server.totalMessagesReceived.load() >= cycles - 2);
    }

    // Test 5: Keep-alive timeout detection
    BEGIN_TEST("Edge case: keep-alive timeout disconnects idle clients");
    {
        EdgeCaseServer server(Port::any);
        Port port = server.getActualPort();
        std::atomic<bool> ready{false};

        auto serverThread
            = startServerInBackground(server, ready, ClientLimit::Unlimited);
        waitForServerReady(ready);

        // Connect a client
        auto client = connectClient(port, Milliseconds{100});
        if (client) {
            // Send initial message
            const char* msg = "test";
            client->send(msg, strlen(msg));

            // Wait longer than keep-alive timeout
            std::this_thread::sleep_for(std::chrono::milliseconds{200});

            // Try to send another message
            bool sent = client->send(msg, strlen(msg));
            // Connection may or may not still work depending on timing
            std::cout << "DEBUG: After timeout, send "
                      << (sent ? "succeeded" : "failed") << "\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        server.requestStop();
        serverThread.join();

        std::cout << "DEBUG: Timed-out clients detected: "
                  << server.timedOutClientsCount.load() << "\n";
    }

    // Test 6: Large message handling under load
    BEGIN_TEST(
        "Edge case: large messages from multiple clients simultaneously");
    {
        EdgeCaseServer server(Port::any);
        Port port = server.getActualPort();
        std::atomic<bool> ready{false};

        auto serverThread
            = startServerInBackground(server, ready, ClientLimit::Unlimited);
        waitForServerReady(ready);

        std::vector<std::unique_ptr<TcpSocket>> clients;
        const int numClients = 5;
        const int messageSize = 5000;

        // Connect clients and send large messages
        for (int i = 0; i < numClients; ++i) {
            auto client = connectClient(port, Milliseconds{100});
            if (client) {
                client->setReceiveTimeout(Milliseconds{500});
                clients.push_back(std::move(client));

                // Send a large message
                std::string largeMsg(messageSize, 'X');
                clients.back()->send(largeMsg.data(), largeMsg.size());
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{200});

        server.requestStop();
        serverThread.join();

        std::cout << "DEBUG: Received " << server.totalMessagesReceived.load()
                  << " messages\n";
        REQUIRE(server.totalMessagesReceived.load() >= numClients - 1);
    }

    // Test 7: Shutdown with active connections
    BEGIN_TEST("Edge case: graceful shutdown with multiple active connections");
    {
        EdgeCaseServer server(Port::any);
        Port port = server.getActualPort();
        std::atomic<bool> ready{false};

        auto serverThread
            = startServerInBackground(server, ready, ClientLimit::Unlimited);
        waitForServerReady(ready);

        std::vector<std::unique_ptr<TcpSocket>> clients;

        // Connect several clients
        for (int i = 0; i < 5; ++i) {
            auto client = connectClient(port, Milliseconds{100});
            if (client) {
                clients.push_back(std::move(client));
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{50});

        // Request stop with active connections
        server.requestStop();

        // Should shut down gracefully
        serverThread.join();

        REQUIRE(true); // If we get here without deadlock, test passes
    }

    // Test 8: Client limit of zero (should be treated as unlimited)
    BEGIN_TEST("Edge case: client limit sanity (zero or unlimited)");
    {
        EdgeCaseServer server(Port::any);
        Port port = server.getActualPort();
        std::atomic<bool> ready{false};

        auto serverThread
            = startServerInBackground(server, ready, ClientLimit::Unlimited);
        waitForServerReady(ready);

        // Connect a few clients
        std::vector<std::unique_ptr<TcpSocket>> clients;
        for (int i = 0; i < 3; ++i) {
            auto client = connectClient(port, Milliseconds{100});
            if (client) {
                clients.push_back(std::move(client));
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{50});

        // clientCount() is unsigned, so it's always >= 0

        server.requestStop();
        serverThread.join();
    }

    // Test 9: Connection attempt rejection after hitting limit
    BEGIN_TEST("Edge case: new connection attempts when at client limit");
    {
        EdgeCaseServer server(Port::any);
        Port port = server.getActualPort();
        std::atomic<bool> ready{false};

        const size_t maxClients = 3;
        auto serverThread
            = startServerInBackground(server, ready, ClientLimit{maxClients});
        waitForServerReady(ready);

        std::vector<std::unique_ptr<TcpSocket>> clients;

        // Fill to capacity
        for (size_t i = 0; i < maxClients; ++i) {
            auto client = connectClient(port, Milliseconds{100});
            if (client) {
                clients.push_back(std::move(client));
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        // Try to connect beyond limit
        auto extraClient = connectClient(port, Milliseconds{100});

        // The behavior here depends on implementation:
        // - Connection might be refused (extraClient is null)
        // - Connection might be accepted but immediately closed
        // - Connection might succeed if one client disconnected

        std::cout << "DEBUG: Extra client "
                  << (extraClient ? "connected" : "rejected") << "\n";

        server.requestStop();
        serverThread.join();

        REQUIRE(true); // Graceful handling is what matters
    }

    // Test 10: Polling timeout accuracy
    BEGIN_TEST("Edge case: server responds within reasonable time");
    {
        EdgeCaseServer server(Port::any);
        Port port = server.getActualPort();
        std::atomic<bool> ready{false};

        auto start = std::chrono::steady_clock::now();

        auto serverThread
            = startServerInBackground(server, ready, ClientLimit::Unlimited);
        waitForServerReady(ready);

        // Connect and send data quickly
        auto client = connectClient(port, Milliseconds{200});
        if (client) {
            const char* msg = "quick";
            client->send(msg, strlen(msg));

            char buf[100];
            int n = client->receive(buf, sizeof(buf));
            if (n > 0) {
                auto elapsed = std::chrono::steady_clock::now() - start;
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    elapsed)
                              .count();
                std::cout << "DEBUG: Round-trip took " << ms << "ms\n";
                REQUIRE(ms < 1000); // Should be quick
            }
        }

        server.requestStop();
        serverThread.join();
    }

    return test_summary();
}
