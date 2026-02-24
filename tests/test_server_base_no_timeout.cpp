// Test ServerBase without keep-alive timeout to isolate segfault

#include "ServerBase.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>

using namespace aiSocks;

struct NoTimeoutState {
    bool dummy{false};
    std::string buf; // Required by ServerBase for debugging
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

    ServerResult onIdle() override {
        return ServerResult::KeepConnection;
    }

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
            server.run(ClientLimit{2}, Milliseconds{100});
        }).detach();

        // Wait for server to be ready
        while (!ready) std::this_thread::sleep_for(std::chrono::milliseconds{10});

        std::cout << "Server started successfully\n";
        
        // Let server run for a bit
        std::this_thread::sleep_for(std::chrono::milliseconds{500});
        
        std::cout << "Stopping server...\n";
        
        // Stop server
        NoTimeoutServer::requestStop();
        
        // Give server time to stop
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        
        std::cout << "Server stopped successfully\n";
    }

    std::cout << "No timeout test completed\n";
    return 0;
}
