// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Minimal test to isolate segfault in ServerBase

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
static void waitForCondition([[maybe_unused]] const std::string& description,
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
            return;
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
}

struct MinimalState {
    bool dummy{false};
    std::string buf; // Required by ServerBase for debugging
};

class MinimalServer : public ServerBase<MinimalState> {
    public:
    explicit MinimalServer(Port port)
        : ServerBase<MinimalState>(ServerBind{"127.0.0.1", port, Backlog{5}}) {}

    std::atomic<size_t> atomicClientCount_{0};

    protected:
    void onClientConnected(TcpSocket&) override {
        atomicClientCount_.fetch_add(1, std::memory_order_relaxed);
    }
    void onClientDisconnected() override {
        atomicClientCount_.fetch_sub(1, std::memory_order_relaxed);
    }

    ServerResult onReadable(TcpSocket& sock, MinimalState& s) override {
        (void)sock;
        (void)s;
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, MinimalState& s) override { //-V524
        (void)sock;
        (void)s;
        return ServerResult::KeepConnection;
    }

    ServerResult onIdle() override { return ServerResult::KeepConnection; }

    void onDisconnect(MinimalState& s) override {
        (void)s; // Suppress unused parameter warning
    }
};

int main() {
    printf("=== Minimal ServerBase Test ===\n");

    BEGIN_TEST("Minimal server with ClientLimit");
    {
        // Reset static stop flag to clean state between test runs
        // (No longer needed with instance variable, but keep for compatibility)
        MinimalServer server(Port::any);
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
            std::this_thread::sleep_for(std::chrono::milliseconds{1});

        // Connect a client to verify server works
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", port, Milliseconds{200}});
        REQUIRE(result.isSuccess());
        auto client = std::make_unique<TcpSocket>(std::move(result.value()));

        // Verify server accepted the client before requesting stop
        waitForCondition("server to accept client",
            [&]() { return server.atomicClientCount_.load() == 1; });

        DLOG("Stopping server...\n");

        // Stop server AFTER it has actually started and accepted a client
        server.requestStop();

        // Wait for server to stop (client goes out of scope)
        waitForCondition(
            "server to stop",
            [&]() {
                return true; // Server should stop quickly
            },
            std::chrono::milliseconds{100});

        DLOG("Server stopped successfully\n");

        // CRITICAL: Wait for server thread to finish before destructor
        serverThread.join();
    }

    return test_summary();
}
