// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Simple test to isolate the segfault issue

#include "ServerBase.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>

using namespace aiSocks;

template <typename Condition>
static bool waitForCondition(Condition&& condition,
    std::chrono::milliseconds maxWait = std::chrono::milliseconds{800},
    std::chrono::milliseconds interval = std::chrono::milliseconds{5}) {
    const auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < maxWait) {
        if (condition()) return true;
        std::this_thread::sleep_for(interval);
    }
    return false;
}

struct SimpleState {
    bool disconnected{false};
    std::string buf; // Required by ServerBase for debugging
};

class SimpleServer : public ServerBase<SimpleState> {
    public:
    explicit SimpleServer(Port port)
        : ServerBase<SimpleState>(ServerBind{"127.0.0.1", port, Backlog{5}}) {}

    std::atomic<size_t> atomicClientCount_{0};

    protected:
    void onClientConnected(TcpSocket&, SimpleState& /*s*/) override {
        atomicClientCount_.fetch_add(1, std::memory_order_relaxed);
    }
    void onClientDisconnected() override {
        atomicClientCount_.fetch_sub(1, std::memory_order_relaxed);
    }

    ServerResult onReadable(TcpSocket& sock, SimpleState& s) override {
        char buf[256];
        int n = sock.receive(buf, sizeof(buf));
        if (n <= 0) {
            s.disconnected = true;
            return ServerResult::Disconnect;
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, SimpleState& s) override {
        (void)sock;
        (void)s;
        return ServerResult::KeepConnection;
    }

    ServerResult onIdle() override { return ServerResult::KeepConnection; }

    void onDisconnect(SimpleState& s) override { s.disconnected = true; }
};

int main() {
    printf("=== Simple ServerBase Test ===\n");

    BEGIN_TEST("ServerBase enforces ClientLimit and tracks connected clients");
    {
        SimpleServer server(Port::any);
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

        // Wait for server to be ready
        const auto readyDeadline
            = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (
            !ready.load() && std::chrono::steady_clock::now() < readyDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        REQUIRE_MSG(ready.load(), "server readiness timed out");

        auto result1 = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{port}, Milliseconds{1000}});
        REQUIRE(result1.isSuccess());
        auto client1 = std::make_unique<TcpSocket>(std::move(result1.value()));

        auto result2 = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{port}, Milliseconds{1000}});
        REQUIRE(result2.isSuccess());
        auto client2 = std::make_unique<TcpSocket>(std::move(result2.value()));

        const bool acceptedTwo = waitForCondition([&]() {
            return server.atomicClientCount_.load(std::memory_order_relaxed)
                == 2;
        });
        REQUIRE(acceptedTwo);

        auto result3 = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{port}, Milliseconds{200}});
        std::unique_ptr<TcpSocket> client3;
        if (result3.isSuccess()) {
            client3 = std::make_unique<TcpSocket>(std::move(result3.value()));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{80});
        REQUIRE(server.atomicClientCount_.load(std::memory_order_relaxed) <= 2);

        REQUIRE(client1->sendAll("abc", 3));
        REQUIRE(client2->sendAll("xyz", 3));

        client1.reset();
        client2.reset();
        client3.reset();

        // Stop server
        server.requestStop();
        serverThread.join();
    }

    return test_summary();
}
