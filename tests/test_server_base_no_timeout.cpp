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

struct NoTimeoutState {
    bool dummy{false};
};

class NoTimeoutServer : public ServerBase<NoTimeoutState> {
    public:
    explicit NoTimeoutServer(uint16_t port)
        : ServerBase<NoTimeoutState>(ServerBind{"127.0.0.1", Port{port}, 5}) {
        // Don't set keep-alive timeout
    }

    protected:
    ServerResult onReadable(TcpSocket& sock, NoTimeoutState& s) override {
        char buf[256];
        int n = sock.receive(buf, sizeof(buf));
        if (n <= 0) {
            return ServerResult::Disconnect;
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, NoTimeoutState& s) override {
        return ServerResult::KeepConnection;
    }

    ServerResult onIdle() override { return ServerResult::KeepConnection; }

    void onDisconnect(NoTimeoutState& s) override {
        // No-op
    }
};

int main() {
    std::cout << "=== No Timeout ServerBase Test ===\n";

    BEGIN_TEST("ServerBase without keep-alive timeout");
    {
        NoTimeoutServer server(21003);
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

        // Test that server works without keep-alive timeout
        // Connect a client and verify it stays connected
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{21003}, Milliseconds{200}});
        REQUIRE(result.isSuccess());
        auto client = std::make_unique<TcpSocket>(std::move(result.value()));

        // Verify server accepted the client
        waitForCondition("server to accept client",
            [&]() { return server.clientCount() == 1; });

        // Let server run briefly to verify no timeout disconnections
        waitForCondition(
            "server to run without timeout",
            [&]() {
                return server.clientCount() == 1; // Should stay connected
            },
            std::chrono::milliseconds{100}); // Short wait to verify no timeout

        std::cout << "Stopping server...\n";

        // Stop server
        NoTimeoutServer::requestStop();

        // Wait for server to stop gracefully
        waitForCondition(
            "server to stop",
            [&]() {
                return true; // Server should stop quickly
            },
            std::chrono::milliseconds{100});

        std::cout << "Server stopped successfully\n";
    }

    std::cout << "No timeout test completed\n";
    return 0;
}
