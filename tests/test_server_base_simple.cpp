// Simple test to isolate the segfault issue

#include "ServerBase.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>

using namespace aiSocks;

struct SimpleState {
    bool disconnected{false};
};

class SimpleServer : public ServerBase<SimpleState> {
public:
    explicit SimpleServer(uint16_t port)
        : ServerBase<SimpleState>(ServerBind{"127.0.0.1", Port{port}, 5}) {}

protected:
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
        return ServerResult::KeepConnection;
    }

    ServerResult onIdle() override {
        return ServerResult::KeepConnection;
    }

    ServerResult onDisconnect(SimpleState& s) override {
        return ServerResult::KeepConnection;
    }
};

int main() {
    std::cout << "=== Simple ServerBase Test ===\n";

    BEGIN_TEST("Basic server with ClientLimit");
    {
        SimpleServer server(21000);
        std::atomic<bool> ready{false};
        
        // Start server with limited clients
        std::thread([&server, &ready]() {
            ready = true;
            server.run(ClientLimit{2}, Milliseconds{100});
        }).detach();

        // Wait for server to be ready
        while (!ready) std::this_thread::sleep_for(std::chrono::milliseconds{10});

        // Connect one client
        auto result = SocketFactory::createTcpClient(
            AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{21000}, Milliseconds{1000}});
        
        if (result.isSuccess()) {
            std::cout << "Client connected successfully\n";
            auto client = std::make_unique<TcpSocket>(std::move(result.value()));
            
            // Give server time to process
            std::this_thread::sleep_for(std::chrono::milliseconds{200});
            
            std::cout << "Server client count: " << server.clientCount() << "\n";
            
            // Disconnect client
            client.reset();
            
            // Give server time to process disconnection
            std::this_thread::sleep_for(std::chrono::milliseconds{200});
            
            std::cout << "Server client count after disconnect: " << server.clientCount() << "\n";
        } else {
            std::cout << "Client connection failed: " << result.message() << "\n";
        }

        // Stop server
        SimpleServer::requestStop();
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    std::cout << "Simple test completed\n";
    return 0;
}
