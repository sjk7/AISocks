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

using namespace aiSocks;

// Helper: Wait for condition with timeout, reporting actual wait time
template <typename Condition>
static bool waitForCondition([[maybe_unused]] const std::string& description,
    Condition&& condition,
    std::chrono::milliseconds maxWait = std::chrono::milliseconds{500},
    std::chrono::milliseconds interval = std::chrono::milliseconds{10}) {
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < maxWait) {
        if (condition()) {
            auto waitTime = std::chrono::steady_clock::now() - startTime;
            DLOG("DEBUG: %s - waited %lldms\n", description.c_str(),
                (long long)
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        waitTime)
                        .count());
            (void)waitTime;
            return true;
        }
        std::this_thread::sleep_for(interval);
    }
    auto waitTime = std::chrono::steady_clock::now() - startTime;
    DLOG("DEBUG: %s - timeout after %lldms (condition not met)\n",
        description.c_str(),
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
            waitTime)
            .count());
    (void)waitTime;
    return false;
}

struct NoTimeoutState {
    bool dummy{false};
    size_t bytesRead{0};
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
    void onClientConnected(TcpSocket&, NoTimeoutState& /*s*/) override {
        atomicClientCount_.fetch_add(1, std::memory_order_relaxed);
    }
    void onClientDisconnected() override {
        atomicClientCount_.fetch_sub(1, std::memory_order_relaxed);
    }

    ServerResult onReadable(TcpSocket& sock, NoTimeoutState& s) override {
        char buf[256];
        int n = sock.receive(buf, sizeof(buf));
        if (n <= 0) {
            return ServerResult::Disconnect;
        }
        s.bytesRead += static_cast<size_t>(n);
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

    BEGIN_TEST(
        "ServerBase keeps idle client alive when keep-alive timeout is unset");
    {
        NoTimeoutServer server(Port::any);
        REQUIRE(server.isValid());
        Port port = Port::any;
        {
            auto ep = server.getSocket().getLocalEndpoint();
            port = ep.isSuccess() ? ep.value().port : Port::any;
        }
        REQUIRE(port.value() > 0);
        std::atomic<bool> ready{false};

        // Start server with limited clients
        std::thread serverThread([&server, &ready]() {
            ready = true;
            server.run(ClientLimit{2}, Milliseconds{1});
        });

        // Wait for server to be ready AND actually accept a client
        const auto readyDeadline
            = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (
            !ready.load() && std::chrono::steady_clock::now() < readyDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        REQUIRE_MSG(ready.load(), "server readiness timed out");

        DLOG("Server started successfully\n");

        // Test that server works without keep-alive timeout
        // Connect a client and verify it stays connected
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{port}, Milliseconds{200}});
        REQUIRE(result.isSuccess());
        auto client = std::make_unique<TcpSocket>(std::move(result.value()));

        const bool accepted = waitForCondition("server to accept client",
            [&]() { return server.atomicClientCount_.load() == 1; });
        REQUIRE(accepted);

        const bool stillConnected = waitForCondition(
            "client remains connected without timeout",
            [&]() { return server.atomicClientCount_.load() == 1; },
            std::chrono::milliseconds{300}, std::chrono::milliseconds{15});
        REQUIRE(stillConnected);

        REQUIRE(client->sendAll("ping", 4));
        const bool stillConnectedAfterTraffic = waitForCondition(
            "client remains connected after traffic",
            [&]() { return server.atomicClientCount_.load() == 1; },
            std::chrono::milliseconds{300}, std::chrono::milliseconds{15});
        REQUIRE(stillConnectedAfterTraffic);

        DLOG("Stopping server...\n");

        client.reset();
        server.requestStop();
        DLOG("Server stopped successfully\n");
        serverThread.join();
    }

    return test_summary();
}
