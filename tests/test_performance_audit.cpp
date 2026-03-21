#include "HttpPollServer.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <iostream>
#include <algorithm>

using namespace aiSocks;

class BenchServer : public HttpPollServer {
    public:
    std::atomic<int> requests{0};
    std::atomic<bool> ready_{false};

    explicit BenchServer(const ServerBind& bind) : HttpPollServer(bind) {
        setHandleSignals(false);
    }

    void onReady() override { ready_ = true; }

    void waitReady() {
        auto start = std::chrono::steady_clock::now();
        while (!ready_.load()
            && std::chrono::steady_clock::now() - start
                < std::chrono::seconds(5)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void buildResponse(HttpClientState& s) override {
        s.dataBuf = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\nConnection: "
                    "keep-alive\r\n\r\nHello, Bench!";
        s.dataView = s.dataBuf;
        requests.fetch_add(1, std::memory_order_relaxed);
    }
};

int main() {
    const int numClients = 5000; // Testing 5k
    const int requestsPerClient = 10;

    std::cout << "Starting Performance Audit (1k connections)..." << std::endl;

    BenchServer server(ServerBind{"127.0.0.1", Port{0, ""}});
    if (!server.isValid()) {
        std::cerr << "Failed to create server" << std::endl;
        return 1;
    }

    std::thread serverThread(
        [&]() { server.run(ClientLimit::Unlimited, Milliseconds{1}); });

    server.waitReady();
    Port port = server.serverPort();
    std::cout << "Server listening on port " << port.value() << std::endl;

    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> clients;
    std::atomic<int> activeClients{0};
    std::atomic<int> failedConnects{0};

    for (int i = 0; i < numClients; ++i) {
        clients.emplace_back([&]() {
            TcpSocket sock = TcpSocket::createRaw(AddressFamily::IPv4);
            if (!sock.connect("127.0.0.1", port)) {
                failedConnects.fetch_add(1);
                return;
            }
            activeClients.fetch_add(1);

            char buf[1024];
            const std::string req
                = "GET /bench HTTP/1.1\r\nHost: localhost\r\nConnection: "
                  "keep-alive\r\n\r\n";

            for (int r = 0; r < requestsPerClient; ++r) {
                if (sock.send(req.data(), req.size()) <= 0) break;
                if (sock.receive(buf, sizeof(buf)) <= 0) break;
            }
        });
    }

    for (auto& t : clients) t.join();

    auto end = std::chrono::steady_clock::now();
    auto diff
        = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();

    server.requestStop();
    serverThread.join();

    std::cout << "Benchmark Results:" << std::endl;
    std::cout << "  Total Time: " << diff << " ms" << std::endl;
    std::cout << "  Active Clients: " << activeClients.load() << std::endl;
    std::cout << "  Failed Connects: " << failedConnects.load() << std::endl;
    std::cout << "  Total Requests: " << server.requests.load() << std::endl;

    if (diff > 0) {
        double rps = (server.requests.load() * 1000.0) / diff;
        std::cout << "  Requests/sec: " << rps << std::endl;
    }

    return 0;
}
