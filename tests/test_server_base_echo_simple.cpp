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

struct SimpleEchoState {
    std::string buf;
    bool disconnected{false};
};

class SimpleEchoServer : public ServerBase<SimpleEchoState> {
    public:
    explicit SimpleEchoServer(Port port)
        : ServerBase<SimpleEchoState>(
              ServerBind{"127.0.0.1", port, Backlog{5}}) {}

    std::atomic<size_t> atomicClientCount_{0};

    protected:
    void onClientConnected(TcpSocket&) override {
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

    BEGIN_TEST("Simple echo server with ClientLimit");
    {
        SimpleEchoServer server(Port::any);
        Port port = Port::any;
        {
            auto ep = server.getSocket().getLocalEndpoint();
            port = ep.isSuccess() ? ep.value().port : Port::any;
        }
        std::atomic<bool> ready{false};

        // Start server with limited clients
        std::thread serverThread([&server, &ready]() {
            ready = true;
            server.run(ClientLimit{2}, Milliseconds{10});
        });

        // Wait for server to be ready
        while (!ready) //-V776 //-V1044
            std::this_thread::sleep_for(std::chrono::milliseconds{10});

        // Connect one client
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", port, Milliseconds{1000}});

        if (result.isSuccess()) {
            printf("Client connected successfully\n");
            auto client
                = std::make_unique<TcpSocket>(std::move(result.value()));

            // Make client non-blocking
            client->setBlocking(false);

            // Send some data
            const char* msg = "Hello Echo!";
            bool sent = client->send(msg, strlen(msg));
            if (sent) {
                printf("Data sent successfully\n");

                // Wait for echo with timeout
                auto start = std::chrono::steady_clock::now();
                const auto timeout = std::chrono::seconds{2};
                char buf[256];
                int received = 0;

                while (received <= 0) {
                    received = client->receive(buf, sizeof(buf));
                    if (received > 0) break;

                    if (client->getLastError() != SocketError::WouldBlock) {
                        break; // Real error
                    }

                    // Check timeout
                    if (std::chrono::steady_clock::now() - start > timeout) {
                        printf("Receive timeout\n");
                        break;
                    }

                    // Small sleep to prevent busy waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds{10});
                }

                if (received > 0) {
                    printf("Received echo: %.*s\n", received, buf);
                }
            }

            printf(
                "Server client count: %zu\n", server.atomicClientCount_.load());
        }

        // Stop server
        server.requestStop();
        serverThread.join();

        printf("Simple echo test completed\n");
    }

    printf("All tests passed!\n");
    return 0;
}
