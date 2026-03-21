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

struct MinimalState {
    bool dummy{false};
    std::string buf; // Required by ServerBase for debugging
};

class MinimalServer : public ServerBase<MinimalState> {
    public:
    explicit MinimalServer(Port port)
        : ServerBase<MinimalState>(ServerBind{"127.0.0.1", port, Backlog{5}}) {}

    std::atomic<size_t> atomicClientCount_{0};
    std::atomic<int> readEvents_{0};

    protected:
    void onClientConnected(TcpSocket&, MinimalState& /*s*/) override {
        atomicClientCount_.fetch_add(1, std::memory_order_relaxed);
    }
    void onClientDisconnected() override {
        atomicClientCount_.fetch_sub(1, std::memory_order_relaxed);
    }

    ServerResult onReadable(TcpSocket& sock, MinimalState& s) override {
        char buf[64];
        const int n = sock.receive(buf, sizeof(buf));
        if (n > 0) {
            readEvents_.fetch_add(1, std::memory_order_relaxed);
            s.buf.append(buf, static_cast<size_t>(n));
        } else if (n < 0 && sock.getLastError() != SocketError::WouldBlock) {
            return ServerResult::Disconnect;
        }
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

    BEGIN_TEST("Minimal server enforces client tracking with ClientLimit");
    {
        MinimalServer server(Port::any);
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

        auto result1 = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", port, Milliseconds{300}});
        REQUIRE(result1.isSuccess());
        auto client1 = std::make_unique<TcpSocket>(std::move(result1.value()));

        auto result2 = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", port, Milliseconds{300}});
        REQUIRE(result2.isSuccess());
        auto client2 = std::make_unique<TcpSocket>(std::move(result2.value()));

        const bool acceptedTwo = waitForCondition(
            "server to accept two clients", [&]() {
                return server.atomicClientCount_.load(std::memory_order_relaxed)
                    == 2;
            });
        REQUIRE(acceptedTwo);

        auto result3 = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", port, Milliseconds{200}});
        std::unique_ptr<TcpSocket> client3;
        if (result3.isSuccess()) {
            client3 = std::make_unique<TcpSocket>(std::move(result3.value()));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{80});
        REQUIRE(server.atomicClientCount_.load(std::memory_order_relaxed) <= 2);

        REQUIRE(client1->sendAll("minimal-read", 12));
        const bool sawRead = waitForCondition("server onReadable fired", [&]() {
            return server.readEvents_.load(std::memory_order_relaxed) > 0;
        });
        REQUIRE(sawRead);

        DLOG("Stopping server...\n");

        client1.reset();
        client2.reset();
        client3.reset();

        server.requestStop();
        DLOG("Server stopped successfully\n");
        serverThread.join();
    }

    return test_summary();
}
