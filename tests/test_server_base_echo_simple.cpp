// Simple echo server test to isolate segfault

#include "ServerBase.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>

using namespace aiSocks;

struct SimpleEchoState {
    std::string buf;
    bool disconnected{false};
};

class SimpleEchoServer : public ServerBase<SimpleEchoState> {
    public:
    explicit SimpleEchoServer(uint16_t port)
        : ServerBase<SimpleEchoState>(ServerBind{"127.0.0.1", Port{port}, 5}) {}

    protected:
    ServerResult onReadable(TcpSocket& sock, SimpleEchoState& s) override {
        char buf[256];
        int n = sock.receive(buf, sizeof(buf));
        if (n > 0) {
            s.buf.assign(buf, n);
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
            server.run(ClientLimit{2}, Milliseconds{100});
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

            // Send some data
            const char* msg = "Hello Echo!";
            bool sent = client->send(msg, std::strlen(msg));
            if (sent) {
                std::cout << "Data sent successfully\n";

                // Give server time to process
                std::this_thread::sleep_for(std::chrono::milliseconds{100});

                // Receive echo
                char buf[256];
                int received = client->receive(buf, sizeof(buf));
                if (received > 0) {
                    std::cout << "Received echo: " << std::string(buf, received)
                              << "\n";
                }
            }

            std::cout << "Server client count: " << server.clientCount()
                      << "\n";
        }

        // Stop server
        SimpleEchoServer::requestStop();
        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        std::cout << "Simple echo test completed\n";
    }

    std::cout << "All tests passed!\n";
    return 0;
}
