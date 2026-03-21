// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Simple echo server test to isolate segfault

#include "ServerBase.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>
#include <cstring>

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

struct SimpleEchoState {
    std::string buf;
    bool disconnected{false};
};

class SimpleEchoServer : public ServerBase<SimpleEchoState> {
    public:
    explicit SimpleEchoServer(Port port)
        : ServerBase<SimpleEchoState>(
              ServerBind{"127.0.0.1", port, Backlog{5, ""}}) {}

    std::atomic<size_t> atomicClientCount_{0};

    protected:
    void onClientConnected(TcpSocket&, SimpleEchoState& /*s*/) override {
        atomicClientCount_.fetch_add(1, std::memory_order_relaxed);
    }
    void onClientDisconnected() override {
        atomicClientCount_.fetch_sub(1, std::memory_order_relaxed);
    }

    ServerResult onReadable(TcpSocket& sock, SimpleEchoState& s) override {
        char buf[256];
        int n = sock.receive(buf, sizeof(buf));
        if (n > 0) {
            s.buf.assign(buf, n);
            // Trigger writable event to send echo back
            setClientWritable(sock, true);
        } else if (n < 0) {
            s.disconnected = true;
            return ServerResult::Disconnect;
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, SimpleEchoState& s) override {
        if (!s.buf.empty()) {
            int sent = sock.send(s.buf.data(), s.buf.size());
            if (sent > 0) {
                s.buf.clear();
                // No more data to send, stop monitoring writable
                setClientWritable(sock, false);
            } else if (sock.getLastError() != SocketError::WouldBlock) {
                s.disconnected = true;
                return ServerResult::Disconnect;
            }
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onIdle() override { return ServerResult::KeepConnection; }

    void onDisconnect(SimpleEchoState& /*s*/) override {}
};

int main() {
    printf("=== Simple Echo ServerBase Test ===\n");

    BEGIN_TEST("Echo server handles multiple clients and echoes payload");
    {
        SimpleEchoServer server(Port::any);
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

        // Connect two clients
        auto result1 = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", port, Milliseconds{1000}});
        REQUIRE(result1.isSuccess());
        auto client1 = std::make_unique<TcpSocket>(std::move(result1.value()));

        auto result2 = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", port, Milliseconds{1000}});
        REQUIRE(result2.isSuccess());
        auto client2 = std::make_unique<TcpSocket>(std::move(result2.value()));

        const bool acceptedTwo = waitForCondition([&]() {
            return server.atomicClientCount_.load(std::memory_order_relaxed)
                == 2;
        });
        REQUIRE(acceptedTwo);

        auto echoRoundTrip = [](TcpSocket& client, const std::string& msg) {
            REQUIRE(client.sendAll(msg.data(), msg.size()));

            const auto start = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::milliseconds{1200};
            char buf[256];
            int received = 0;
            while (received <= 0
                && std::chrono::steady_clock::now() - start < timeout) {
                received = client.receive(buf, sizeof(buf));
                if (received > 0) break;
                if (client.getLastError() != SocketError::WouldBlock
                    && client.getLastError() != SocketError::Timeout) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
            }

            REQUIRE(received == static_cast<int>(msg.size()));
            if (received > 0) {
                REQUIRE(std::string(buf, static_cast<size_t>(received)) == msg);
            }
        };

        client1->setReceiveTimeout(Milliseconds{1000});
        client2->setReceiveTimeout(Milliseconds{1000});
        echoRoundTrip(*client1, "Hello Echo 1!");
        echoRoundTrip(*client2, "Hello Echo 2!");

        auto result3 = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", port, Milliseconds{200}});
        std::unique_ptr<TcpSocket> client3;
        if (result3.isSuccess()) {
            client3 = std::make_unique<TcpSocket>(std::move(result3.value()));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{80});
        REQUIRE(server.atomicClientCount_.load(std::memory_order_relaxed) <= 2);

        client1.reset();
        client2.reset();
        client3.reset();

        // Stop server
        server.requestStop();
        serverThread.join();
    }

    return test_summary();
}
