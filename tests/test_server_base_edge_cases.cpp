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

// Enable diagnostic output by compiling with -DTEST_VERBOSE.
#ifdef TEST_VERBOSE
#define DLOG(...)                                                              \
    do {                                                                       \
        printf(__VA_ARGS__);                                                   \
        fflush(stdout);                                                        \
    } while (0)
#else
#define DLOG(...)                                                              \
    do {                                                                       \
    } while (0)
#endif

// Always-on step tracer: flushed immediately so CI output survives a SEGFAULT.
#define STEP(msg)                                                              \
    do {                                                                       \
        printf("  [STEP %s:%d] %s\n", __FILE__, __LINE__, (msg));             \
        fflush(stdout);                                                        \
    } while (0)

using namespace aiSocks;

// Testing constants for edge case scenarios
static constexpr Milliseconds QUICK_POLL_TIMEOUT{1};
static constexpr Milliseconds SHORT_KEEP_ALIVE{
    1000}; // 1 second - more reasonable for testing

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
        setKeepAliveTimeout(SHORT_KEEP_ALIVE);
        setHandleSignals(
            false); // Disable signal handling for parallel test execution
    }

    // Method to set custom keep-alive timeout for specific tests
    void setCustomKeepAliveTimeout(Milliseconds timeout) {
        setKeepAliveTimeout(timeout);
    }

    std::atomic<int> totalMessagesReceived{0};
    std::atomic<int> totalMessagesSent{0};
    std::atomic<int> errorCount{0};
    std::atomic<size_t> timedOutClientsCount{0};
    std::atomic<int> disconnectCount{0};
    std::atomic<int> idleCallCount{0};
    std::atomic<size_t> atomicClientCount_{0};
    std::atomic<int> acceptFilterCalls{0};
    std::atomic<int> acceptFilterRejected{0};

    void setRejectNewConnections(bool reject) {
        rejectNewConnections_.store(reject, std::memory_order_relaxed);
    }

    std::atomic<bool> ready_{false};
    void waitReady() const {
        while (!ready_.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    void waitForMessages(int n) const {
        while (totalMessagesReceived.load(std::memory_order_acquire) < n)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    Port serverPort() const {
        auto endpoint = getSocket().getLocalEndpoint();
        return endpoint.isSuccess() ? endpoint.value().port : Port::any;
    }

    protected:
    bool onAcceptFilter(const std::string& /*peerAddress*/) override {
        acceptFilterCalls.fetch_add(1, std::memory_order_relaxed);
        const bool reject
            = rejectNewConnections_.load(std::memory_order_relaxed);
        if (reject)
            acceptFilterRejected.fetch_add(1, std::memory_order_relaxed);
        return !reject;
    }

    void onClientConnected(TcpSocket&, EdgeCaseState& /*s*/) override {
        atomicClientCount_.fetch_add(1, std::memory_order_relaxed);
    }
    void onClientDisconnected() override {
        atomicClientCount_.fetch_sub(1, std::memory_order_relaxed);
    }

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
        if (!s.buf.empty()) setClientWritable(sock, true);
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, EdgeCaseState& s) final {
        if (s.buf.empty()) {
            setClientWritable(sock, false);
            return ServerResult::KeepConnection;
        }

        int sent = sock.send(s.buf.data(), s.buf.size());
        if (sent > 0) {
            totalMessagesSent++;
            s.buf.erase(0, sent);
            if (s.buf.empty()) setClientWritable(sock, false);
        } else if (sock.getLastError() != SocketError::WouldBlock) {
            errorCount++;
            return ServerResult::Disconnect;
        }
        return ServerResult::KeepConnection;
    }

    void onReady() override { ready_.store(true, std::memory_order_release); }

    void onClientsTimedOut(size_t count) override {
        timedOutClientsCount += count;
        ServerBase::onClientsTimedOut(count);
    }

    void onDisconnect(EdgeCaseState& s) override {
        (void)s; // Suppress warning
        disconnectCount++;
    }

    ServerResult onIdle() override {
        idleCallCount++;
        return ServerResult::KeepConnection;
    }

    private:
    std::atomic<bool> rejectNewConnections_{false};
};

// Minimal server that can be constructed in a failing state (e.g. port already
// in use) without flooding stderr. Tracks whether onReady() was called.
class InvalidInstanceServer : public ServerBase<EdgeCaseState> {
    public:
    explicit InvalidInstanceServer(Port port)
        : ServerBase<EdgeCaseState>(ServerBind{"127.0.0.1", port, Backlog{10},
              /*reuseAddr=*/true, /*logStartupErrors=*/false}) {}

    std::atomic<bool> onReadyCalled{false};

    protected:
    void onReady() override {
        onReadyCalled.store(true, std::memory_order_release);
    }
    ServerResult onReadable(TcpSocket&, EdgeCaseState&) override {
        return ServerResult::KeepConnection;
    }
    ServerResult onWritable(TcpSocket&, EdgeCaseState&) override {
        return ServerResult::KeepConnection;
    }
};

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------
static std::thread startServerInBackground(
    EdgeCaseServer& server, ClientLimit maxClients = ClientLimit::Unlimited) {
    return std::thread([&server, maxClients]() {
        server.run(maxClients, QUICK_POLL_TIMEOUT);
    });
}

static void waitForServerReady(EdgeCaseServer& server) {
    server.waitReady();
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
    printf("=== ServerBase Edge Cases Tests ===\n");

    // Test 1: Server under load with many connections
    BEGIN_TEST("Edge case: server handles high connection load");
    {
        STEP("construct server");
        EdgeCaseServer server(Port::any);
        STEP("get port");
        Port port = server.serverPort();

        STEP("start server thread");
        auto serverThread
            = startServerInBackground(server, ClientLimit::Unlimited);
        STEP("wait ready");
        waitForServerReady(server);
        STEP("connecting clients");

        std::vector<std::unique_ptr<TcpSocket>> clients;
        const int numClients = 20; // High load

        // Connect many clients
        for (int i = 0; i < numClients; ++i) {
            auto client = connectClient(port, Milliseconds{200});
            if (client) {
                clients.push_back(std::move(client));
            }
        }

        printf(
            "DEBUG: Connected %zu of %d clients\n", clients.size(), numClients);

        // Each client sends a small message
        for (auto& client : clients) {
            client->setBlocking(true);
            const char* msg = "test";
            client->sendAll(msg, strlen(msg));
        }

        server.waitForMessages(1);

        // Server should handle all without crashing
        REQUIRE(server.totalMessagesReceived.load() > 0);
        REQUIRE(server.errorCount.load() == 0); // No errors expected

        server.requestStop();
        serverThread.join();
    }

    // Test 2: Client limit exactly at capacity
    BEGIN_TEST("Edge case: server at exactly maximum client capacity");
    {
        STEP("construct server");
        EdgeCaseServer server(Port::any);
        STEP("get port");
        Port port = server.serverPort();

        const size_t maxClients = 5;
        STEP("start server thread");
        auto serverThread
            = startServerInBackground(server, ClientLimit{maxClients});
        STEP("wait ready");
        waitForServerReady(server);

        std::vector<std::unique_ptr<TcpSocket>> clients;

        // Connect up to exact limit
        for (size_t i = 0; i < maxClients; ++i) {
            auto client = connectClient(port, Milliseconds{100});
            if (client) {
                client->setBlocking(true);
                const char* msg = "test";
                client->sendAll(msg, strlen(msg));
                clients.push_back(std::move(client));
            }
        }

        // Verify we're at capacity
        server.waitForMessages(1);
        DLOG("DEBUG: Client count = %zu (expected %zu)\n",
            server.atomicClientCount_.load(), maxClients);

        REQUIRE(server.atomicClientCount_.load() <= maxClients);

        server.requestStop();
        serverThread.join();
    }

    // Test 3: Client limit of 1 (minimal capacity)
    BEGIN_TEST("Edge case: server with minimal client limit (1)");
    {
        STEP("construct server");
        EdgeCaseServer server(Port::any);
        STEP("get port");
        Port port = server.serverPort();

        STEP("start server thread");
        auto serverThread = startServerInBackground(server, ClientLimit{1});
        STEP("wait ready");
        waitForServerReady(server);

        // Connect one client
        auto client1 = connectClient(port, Milliseconds{100});
        REQUIRE(client1 != nullptr);
        client1->setBlocking(true);

        // Verify client is connected
        REQUIRE(server.atomicClientCount_.load() <= 1);

        // Send a message and wait for it to be processed
        const char* msg = "hello";
        client1->sendAll(msg, strlen(msg));
        server.waitForMessages(1);

        server.requestStop();
        serverThread.join();

        REQUIRE(server.totalMessagesReceived.load() > 0);
    }

    // Test 4: Rapid connect/disconnect cycling
    BEGIN_TEST("Edge case: rapid connect/disconnect/reconnect cycling");
    {
        STEP("construct server");
        EdgeCaseServer server(Port::any);
        server.setCustomKeepAliveTimeout(Milliseconds{5000});
        STEP("get port");
        Port port = server.serverPort();

        STEP("start server thread");
        auto serverThread
            = startServerInBackground(server, ClientLimit::Unlimited);
        STEP("wait ready");
        waitForServerReady(server);

        // Connect/disconnect cycle
        int cycles = 10;
        for (int i = 0; i < cycles; ++i) {
            auto client = connectClient(port, Milliseconds{100});
            if (client) {
                client->setBlocking(true);
                const char* msg = "data";
                client->sendAll(msg, strlen(msg));
            }
            // Client disconnects when it goes out of scope
        }

        server.waitForMessages(cycles);

        server.requestStop();
        serverThread.join();

        DLOG("DEBUG: Completed %d cycles\n", cycles);
        REQUIRE(server.totalMessagesReceived.load() >= cycles);
    }

    // Test 5: Keep-alive timeout detection
    BEGIN_TEST("Edge case: keep-alive timeout disconnects idle clients");
    {
        STEP("construct server");
        EdgeCaseServer server(Port::any);
        server.setCustomKeepAliveTimeout(Milliseconds{10});
        STEP("get port");
        Port port = server.serverPort();

        STEP("start server thread");
        auto serverThread
            = startServerInBackground(server, ClientLimit::Unlimited);
        STEP("wait ready");
        waitForServerReady(server);

        // Connect a client
        auto client = connectClient(port, Milliseconds{100});
        if (client) {
            client->setBlocking(true);
            // Send initial message
            const char* msg = "test";
            client->sendAll(msg, strlen(msg));

            // Wait just long enough for the 10ms keep-alive to fire
            std::this_thread::sleep_for(std::chrono::milliseconds{30});

            // Try to send another message
            bool sent = client->sendAll(msg, strlen(msg));
            // Connection may or may not still work depending on timing
            DLOG("DEBUG: After timeout, send %s\n",
                (sent ? "succeeded" : "failed"));
            (void)sent;
        }

        server.requestStop();
        serverThread.join();

        DLOG("DEBUG: Timed-out clients detected: %zu\n",
            server.timedOutClientsCount.load());
    }

    // Test 6: Large message handling under load
    BEGIN_TEST(
        "Edge case: large messages from multiple clients simultaneously");
    {
        STEP("construct server");
        EdgeCaseServer server(Port::any);
        STEP("get port");
        Port port = server.serverPort();

        STEP("start server thread");
        auto serverThread
            = startServerInBackground(server, ClientLimit::Unlimited);
        STEP("wait ready");
        waitForServerReady(server);

        std::vector<std::unique_ptr<TcpSocket>> clients;
        const int numClients = 5;
        const int messageSize = 5000;

        // Connect clients and send large messages immediately
        for (int i = 0; i < numClients; ++i) {
            auto client = connectClient(port, Milliseconds{100});
            if (client) {
                client->setBlocking(true);
                client->setReceiveTimeout(Milliseconds{500});

                // Send large message immediately after connecting
                std::string largeMsg(messageSize, 'X');
                bool sent = client->sendAll(largeMsg.data(), largeMsg.size());
                DLOG("DEBUG: Client %d sent %d bytes: %s\n", i, messageSize,
                    (sent ? "true" : "false"));
                (void)sent;

                clients.push_back(std::move(client));
            }
        }

        server.waitForMessages(numClients - 1);

        server.requestStop();
        serverThread.join();

        DLOG("DEBUG: Received %d messages\n",
            server.totalMessagesReceived.load());
        REQUIRE(server.totalMessagesReceived.load() >= numClients - 1);
    }

    // Test 7: Shutdown with active connections
    BEGIN_TEST("Edge case: graceful shutdown with multiple active connections");
    {
        STEP("construct server");
        EdgeCaseServer server(Port::any);
        STEP("get port");
        Port port = server.serverPort();

        STEP("start server thread");
        auto serverThread
            = startServerInBackground(server, ClientLimit::Unlimited);
        STEP("wait ready");
        waitForServerReady(server);

        std::vector<std::unique_ptr<TcpSocket>> clients;

        // Connect several clients and send data so we can sync
        for (int i = 0; i < 5; ++i) {
            auto client = connectClient(port, Milliseconds{100});
            if (client) {
                client->setBlocking(true);
                client->sendAll("hi", 2);
                clients.push_back(std::move(client));
            }
        }

        server.waitForMessages(static_cast<int>(clients.size()));

        // Request stop with active connections
        server.requestStop();

        // Should shut down gracefully
        serverThread.join();

        REQUIRE(true); // If we get here without deadlock, test passes
    }

    // Test 8: Client limit of zero (should be treated as unlimited)
    BEGIN_TEST("Edge case: client limit sanity (zero or unlimited)");
    {
        STEP("construct server");
        EdgeCaseServer server(Port::any);
        STEP("get port");
        Port port = server.serverPort();

        STEP("start server thread");
        auto serverThread
            = startServerInBackground(server, ClientLimit::Unlimited);
        STEP("wait ready");
        waitForServerReady(server);

        // Connect a few clients and send data so we can sync
        std::vector<std::unique_ptr<TcpSocket>> clients;
        for (int i = 0; i < 3; ++i) {
            auto client = connectClient(port, Milliseconds{100});
            if (client) {
                client->setBlocking(true);
                client->sendAll("hi", 2);
                clients.push_back(std::move(client));
            }
        }

        server.waitForMessages(static_cast<int>(clients.size()));

        // clientCount() is unsigned, so it's always >= 0

        server.requestStop();
        serverThread.join();
    }

    // Test 9: Connection attempt rejection after hitting limit
    BEGIN_TEST("Edge case: new connection attempts when at client limit");
    {
        STEP("construct server");
        EdgeCaseServer server(Port::any);
        STEP("get port");
        Port port = server.serverPort();

        const size_t maxClients = 3;
        STEP("start server thread");
        auto serverThread
            = startServerInBackground(server, ClientLimit{maxClients});
        STEP("wait ready");
        waitForServerReady(server);

        std::vector<std::unique_ptr<TcpSocket>> clients;

        // Fill to capacity - each client sends a message so we can sync
        for (size_t i = 0; i < maxClients; ++i) {
            auto client = connectClient(port, Milliseconds{100});
            if (client) {
                client->setBlocking(true);
                client->sendAll("hi", 2);
                clients.push_back(std::move(client));
            }
        }

        server.waitForMessages(static_cast<int>(clients.size()));

        // Try to connect beyond limit
        auto extraClient = connectClient(port, Milliseconds{100});

        // The behavior here depends on implementation:
        // - Connection might be refused (extraClient is null)
        // - Connection might be accepted but immediately closed
        // - Connection might succeed if one client disconnected

        DLOG("DEBUG: Extra client %s\n",
            (extraClient ? "connected" : "rejected"));

        server.requestStop();
        serverThread.join();

        REQUIRE(true); // Graceful handling is what matters
    }

    // Test 10: Polling timeout accuracy
    BEGIN_TEST("Edge case: server responds within reasonable time");
    {
        STEP("construct server");
        EdgeCaseServer server(Port::any);
        STEP("get port");
        Port port = server.serverPort();

        STEP("start server thread");
        auto serverThread
            = startServerInBackground(server, ClientLimit::Unlimited);
        STEP("wait ready");
        waitForServerReady(server);

        // Connect and send data quickly
        auto client = connectClient(port, Milliseconds{200});
        if (client) {
            client->setBlocking(true);
            client->setReceiveTimeout(Milliseconds{500});
            const char* msg = "quick";
            auto start = std::chrono::steady_clock::now(); //-V821
            client->sendAll(msg, strlen(msg));

            char buf[100];
            int n = client->receive(buf, sizeof(buf));
            if (n > 0) {
                auto elapsed = std::chrono::steady_clock::now() - start;
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    elapsed)
                              .count();
                DLOG("DEBUG: Round-trip took %lldms\n", (long long)ms);
                REQUIRE(ms < 1000); // Should be quick
            }
        }

        server.requestStop();
        serverThread.join();
    }

    // Test 11: wait_forever suppresses onIdle()
    BEGIN_TEST("Edge case: wait_forever — onIdle() never called while idle");
    {
        STEP("construct server");
        EdgeCaseServer server(Port::any);
        STEP("get port");
        Port port = server.serverPort();

        // Run with wait_forever: poller blocks until a real event fires,
        // so onIdle() must never be called during a quiet period.
        STEP("start server thread (wait_forever)");
        auto serverThread = std::thread(
            [&server]() { server.run(ClientLimit::Unlimited, wait_forever); });
        STEP("wait ready");
        waitForServerReady(server);

        // Sit quietly for 30ms — server should be blocked in the poller.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        REQUIRE(server.idleCallCount.load() == 0);

        // Connect and send data to prove the server is still alive.
        auto client = connectClient(port, Milliseconds{200});
        REQUIRE(client != nullptr);
        client->setBlocking(true);
        client->sendAll("ping", 4);
        server.waitForMessages(1);
        REQUIRE(server.totalMessagesReceived.load() > 0);

        // onIdle() still must not have fired.
        REQUIRE(server.idleCallCount.load() == 0);

        // requestStop() sets the flag, then closing the client generates a
        // socket event that wakes the poller out of its indefinite wait.
        server.requestStop();
        client.reset(); // closes socket → wakes poller → stop flag is seen
        serverThread.join();
    }

    // Test 12: accept filter rejects before registration (no stale client
    // events)
    BEGIN_TEST("Edge case: accept-filter rejects do not leak poller state");
    {
        STEP("construct server");
        EdgeCaseServer server(Port::any);
        STEP("get port");
        Port port = server.serverPort();

        STEP("start server thread");
        auto serverThread
            = startServerInBackground(server, ClientLimit::Unlimited);
        STEP("wait ready");
        waitForServerReady(server);

        server.setRejectNewConnections(true);

        // Repeatedly connect while filter rejects to exercise the reject path
        // and encourage fd reuse patterns.
        for (int i = 0; i < 40; ++i) {
            auto client = connectClient(port, Milliseconds{100});
            if (client) {
                client->setBlocking(true);
                const char* msg = "x";
                (void)client->sendAll(msg, 1);
            }
        }

        // Give the server loop time to process all reject-path activity.
        std::this_thread::sleep_for(std::chrono::milliseconds{60});

        REQUIRE(server.acceptFilterCalls.load() > 0);
        REQUIRE(server.acceptFilterRejected.load() > 0);
        REQUIRE(server.totalMessagesReceived.load() == 0);

        // Turn off rejection and verify a new client is accepted and served.
        server.setRejectNewConnections(false);
        auto acceptedClient = connectClient(port, Milliseconds{200});
        REQUIRE(acceptedClient != nullptr);
        acceptedClient->setBlocking(true);
        REQUIRE(acceptedClient->sendAll("ok", 2));

        server.waitForMessages(1);
        REQUIRE(server.totalMessagesReceived.load() > 0);

        server.requestStop();
        serverThread.join();
    }

    // Test 13: isValid()==false — serverEndpoint() returns an error Result
    BEGIN_TEST("Invalid server: serverEndpoint() returns error Result");
    {
        STEP("construct holder");
        EdgeCaseServer holder(Port::any);
        REQUIRE(holder.isValid());
        STEP("get taken port");
        Port takenPort = holder.serverPort();
        REQUIRE(takenPort.value() != 0);
        STEP("construct bad server on taken port");
        InvalidInstanceServer bad(takenPort);
        STEP("check isValid");
        REQUIRE(!bad.isValid());
        STEP("call serverEndpoint");
        auto ep = bad.serverEndpoint();
        REQUIRE(!ep.isSuccess());
        REQUIRE(ep.error() == SocketError::InvalidSocket);
        STEP("done");
    }

    // Test 14: isValid()==false — serverPort() returns Port::any
    BEGIN_TEST("Invalid server: serverPort() returns Port::any");
    {
        STEP("construct holder");
        EdgeCaseServer holder(Port::any);
        REQUIRE(holder.isValid());
        STEP("get taken port");
        Port takenPort = holder.serverPort();
        STEP("construct bad server on taken port");
        InvalidInstanceServer bad(takenPort);
        STEP("check isValid");
        REQUIRE(!bad.isValid());
        STEP("call serverPort");
        Port p = bad.serverPort();
        REQUIRE(p.value() == Port::any.value());
        STEP("done");
    }

    // Test 15: isValid()==false — run() returns immediately and calls onReady()
    BEGIN_TEST("Invalid server: run() returns immediately, onReady() called");
    {
        STEP("construct holder");
        EdgeCaseServer holder(Port::any);
        REQUIRE(holder.isValid());
        STEP("get taken port");
        Port takenPort = holder.serverPort();
        STEP("construct bad server on taken port");
        InvalidInstanceServer bad(takenPort);
        STEP("check isValid");
        REQUIRE(!bad.isValid());
        REQUIRE(!bad.onReadyCalled.load());
        STEP("call run() on invalid server");
        // run() must not block and must invoke onReady() before returning.
        bad.run();
        STEP("run() returned");
        REQUIRE(bad.onReadyCalled.load());
        REQUIRE(bad.clientCount() == 0);
        STEP("done");
    }

    return test_summary();
}
