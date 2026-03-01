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
    explicit SimpleEchoServer(uint16_t port)
        : ServerBase<SimpleEchoState>(
              ServerBind{"127.0.0.1", Port{port}, Backlog{5}}) {}

    protected:
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
    std::cout << "=== Simple Echo ServerBase Test ===\n";

    BEGIN_TEST("Simple echo server with ClientLimit");
    {
        SimpleEchoServer server(21002);
        std::atomic<bool> ready{false};

        // Start server with limited clients
        std::thread([&server, &ready]() {
            ready = true;
            server.run(ClientLimit{2}, Milliseconds{10});
        }).detach();

        // Wait for server to be ready
        while (!ready)
            std::this_thread::sleep_for(std::chrono::milliseconds{10});

        // Connect one client
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{21002}, Milliseconds{1000}});

        if (result.isSuccess()) {
            std::cout << "Client connected successfully\n";
            auto client
                = std::make_unique<TcpSocket>(std::move(result.value()));

            // Make client non-blocking
            client->setBlocking(false);

            // Send some data
            const char* msg = "Hello Echo!";
            bool sent = client->send(msg, std::strlen(msg));
            if (sent) {
                std::cout << "Data sent successfully\n";

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
                        std::cout << "Receive timeout\n";
                        break;
                    }

                    // Small sleep to prevent busy waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds{10});
                }

                if (received > 0) {
                    std::cout << "Received echo: " << std::string(buf, received)
                              << "\n";
                }
            }

            std::cout << "Server client count: " << server.clientCount()
                      << "\n";
        }

        // Stop server
        server.requestStop();
        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        std::cout << "Simple echo test completed\n";
    }

    std::cout << "All tests passed!\n";
    return 0;
}
