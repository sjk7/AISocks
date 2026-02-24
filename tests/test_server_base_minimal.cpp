// Minimal test to isolate segfault in ServerBase

#include "ServerBase.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>

using namespace aiSocks;

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

    ServerResult onIdle() override {
        return ServerResult::KeepConnection;
    }

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
            server.run(ClientLimit{2}, Milliseconds{100});
        }).detach();

        // Wait for server to be ready
        while (!ready) std::this_thread::sleep_for(std::chrono::milliseconds{10});

        std::cout << "Server started successfully\n";
        
        // Let server run for a bit
        std::this_thread::sleep_for(std::chrono::milliseconds{500});
        
        std::cout << "Stopping server...\n";
        
        // Stop server
        MinimalServer::requestStop();
        
        // Give server time to stop
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        
        std::cout << "Server stopped successfully\n";
    }

    std::cout << "Minimal test completed\n";
    return 0;
}
