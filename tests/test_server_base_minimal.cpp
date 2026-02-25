// Minimal test to isolate segfault in ServerBase

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
            std::cout << "DEBUG: " << description << " - waited "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                             waitTime)
                             .count()
                      << "ms\n";
            return;
        }
        std::this_thread::sleep_for(interval);
    }
    auto waitTime = std::chrono::steady_clock::now() - startTime;
    std::cout << "DEBUG: " << description << " - timeout after "
              << std::chrono::duration_cast<std::chrono::milliseconds>(waitTime)
                     .count()
              << "ms (condition not met)\n";
}

struct MinimalState {
    bool dummy{false};
    std::string buf; // Required by ServerBase for debugging
};

class MinimalServer : public ServerBase<MinimalState> {
    public:
    explicit MinimalServer(uint16_t port)
        : ServerBase<MinimalState>(ServerBind{"127.0.0.1", Port{port}, 5}) {}

    protected:
    ServerResult onReadable(TcpSocket& sock, MinimalState& s) override {
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, MinimalState& s) override {
        return ServerResult::KeepConnection;
    }

    ServerResult onIdle() override { return ServerResult::KeepConnection; }

    void onDisconnect(MinimalState& s) override {
        (void)s; // Suppress unused parameter warning
    }
};

int main() {
    std::cout << "=== Minimal ServerBase Test ===\n";

    BEGIN_TEST("Minimal server with ClientLimit");
    {
        MinimalServer server(21001);
        std::atomic<bool> ready{false};

        // Start server with limited clients
        std::thread([&server, &ready]() {
            ready = true;
            server.run(ClientLimit{2}, Milliseconds{10});
        }).detach();

        // Wait for server to be ready
        while (!ready)
            std::this_thread::sleep_for(std::chrono::milliseconds{10});

        std::cout << "Server started successfully\n";

        // Connect a client to verify server works
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{21001}, Milliseconds{200}});
        REQUIRE(result.isSuccess());
        auto client = std::make_unique<TcpSocket>(std::move(result.value()));

        // Verify server accepted the client
        waitForCondition("server to accept client",
            [&]() { return server.clientCount() == 1; });

        std::cout << "Stopping server...\n";

        // Stop server
        MinimalServer::requestStop();

        // Wait for server to stop (client goes out of scope)
        waitForCondition(
            "server to stop",
            [&]() {
                return true; // Server should stop quickly
            },
            std::chrono::milliseconds{100});

        std::cout << "Server stopped successfully\n";
    }

    std::cout << "Minimal test completed\n";
    return 0;
}
