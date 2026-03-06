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

struct SimpleState {
    bool disconnected{false};
    std::string buf; // Required by ServerBase for debugging
};

class SimpleServer : public ServerBase<SimpleState> {
    public:
    explicit SimpleServer(Port port)
        : ServerBase<SimpleState>(ServerBind{"127.0.0.1", port, Backlog{5}}) {}

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
        (void)sock;
        (void)s;
        return ServerResult::KeepConnection;
    }

    ServerResult onIdle() override { return ServerResult::KeepConnection; }

    void onDisconnect(SimpleState& s) override { s.disconnected = true; }
};

int main() {
    printf("=== Simple ServerBase Test ===\n");

    BEGIN_TEST("Basic server with ClientLimit");
    {
        SimpleServer server(Port::any);
        Port port = Port::any;
        {
            auto ep = server.getSocket().getLocalEndpoint();
            port = ep.isSuccess() ? ep.value().port : Port::any;
        }
        std::atomic<bool> ready{false};

        // Start server with limited clients
        std::thread([&server, &ready]() {
            ready = true;
            server.run(ClientLimit{2}, Milliseconds{10});
        }).detach();

        // Wait for server to be ready
        while (!ready) //-V776 //-V1044
            std::this_thread::sleep_for(std::chrono::milliseconds{10});

        // Connect one client
        auto result = SocketFactory::createTcpClient(AddressFamily::IPv4,
            ConnectArgs{"127.0.0.1", Port{port}, Milliseconds{1000}});

        if (result.isSuccess()) {
            printf("Client connected successfully\n");
            auto client
                = std::make_unique<TcpSocket>(std::move(result.value()));

            // Give server time to process
            std::this_thread::sleep_for(std::chrono::milliseconds{200});

            printf("Server client count: %zu\n", server.clientCount());

            // Disconnect client
            client.reset();

            // Give server time to process disconnection
            std::this_thread::sleep_for(std::chrono::milliseconds{200});

            printf("Server client count after disconnect: %zu\n",
                server.clientCount());
        } else {
            printf("Client connection failed: %s\n", result.message().c_str());
        }

        // Stop server
        server.requestStop();
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    printf("Simple test completed\n");
    return 0;
}
