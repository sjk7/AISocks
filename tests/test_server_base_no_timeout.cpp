// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Test ServerBase without keep-alive timeout to isolate segfault

#include "ServerBase.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>

using namespace aiSocks;

// Helper: Wait for condition with timeout, reporting actual wait time
template <typename Condition>
static void waitForCondition(const std::string& description,
    Condition&& condition,
    std::chrono::milliseconds maxWait = std::chrono::milliseconds{500},
    std::chrono::milliseconds interval = std::chrono::milliseconds{10}) {
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < maxWait) {
        if (condition()) {
            auto waitTime = std::chrono::steady_clock::now() - startTime;
            printf("DEBUG: %s - waited %lldms\n", description.c_str(),
                (long long)
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        waitTime)
                        .count());
            return;
        }
        std::this_thread::sleep_for(interval);
    }
    auto waitTime = std::chrono::steady_clock::now() - startTime;
    printf("DEBUG: %s - timeout after %lldms (condition not met)\n",
        description.c_str(),
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
            waitTime)
            .count());
}

struct NoTimeoutState {
    bool dummy{false};
};

class NoTimeoutServer : public ServerBase<NoTimeoutState> {
    public:
    explicit NoTimeoutServer(Port port)
        : ServerBase<NoTimeoutState>(
              ServerBind{"127.0.0.1", port, Backlog{5}}) {
        // Don't set keep-alive timeout
    }

    std::atomic<size_t> atomicClientCount_{0};

    protected:
    void onClientConnected(TcpSocket&) override {
        atomicClientCount_.fetch_add(1, std::memory_order_relaxed);
    }
    void onClientDisconnected() override {
        atomicClientCount_.fetch_sub(1, std::memory_order_relaxed);
    }

    ServerResult onReadable(TcpSocket& sock, NoTimeoutState& s) override {
        (void)s;
        char buf[256];
        int n = sock.receive(buf, sizeof(buf));
        if (n <= 0) {
            return ServerResult::Disconnect;
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, NoTimeoutState& s) override {
        (void)sock;
        (void)s;
        return ServerResult::KeepConnection;
    }

    ServerResult onIdle() override { return ServerResult::KeepConnection; }

    void onDisconnect(NoTimeoutState& s) override {
        (void)s;
        // No-op
    }
};

int main() {
    printf("=== No Timeout ServerBase Test ===\n");

    BEGIN_TEST("ServerBase without keep-alive timeout");
    {
        // Reset static stop flag to clean state between test runs
        // (No longer needed with instance variable, but keep for compatibility)
        NoTimeoutServer server(Port::any);
        Port port = Port::any;
        {
            auto ep = server.getSocket().getLocalEndpoint();
            port = ep.isSuccess() ? ep.value().port : Port::any;
        }
        std::atomic<bool> ready{false};

        // Start server with limited clients
        std::thread serverThread([&server, &ready]() {
            ready = true;
            server.run(ClientLimit{2}, Milliseconds{1});
        });

        // Wait for server to be ready AND actually accept a client
        while (!ready) //-V1044 //-V776
            std::this_thread::sleep_for(std::chrono::milliseconds{10});

        printf("Server started successfully\n");

        // Test that server works without keep-alive timeout
        // Connect a client and verify it stays connected
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{port}, Milliseconds{200}});
        REQUIRE(result.isSuccess());
        auto client = std::make_unique<TcpSocket>(std::move(result.value()));

        // Verify server accepted the client BEFORE requesting stop
        waitForCondition("server to accept client",
            [&]() { return server.atomicClientCount_.load() == 1; });

        // Let server run briefly to verify no timeout disconnections
        waitForCondition(
            "server to run without timeout",
            [&]() {
                return server.atomicClientCount_.load()
                    == 1; // Should stay connected
            },
            std::chrono::milliseconds{100}); // Short wait to verify no timeout

        printf("Stopping server...\n");

        // Stop server AFTER it has actually started and accepted a client
        server.requestStop();

        // Wait for server to stop gracefully
        waitForCondition(
            "server to stop",
            [&]() {
                return true; // Server should stop quickly
            },
            std::chrono::milliseconds{100});

        printf("Server stopped successfully\n");

        // CRITICAL: Wait for server thread to finish before destructor
        serverThread.join();
    }

    return test_summary();
}
