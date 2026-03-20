// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Integration test: UDP service discovery -> TCP HTTP request
//
// Pattern:
//   1. HttpPollServer starts on Port::any (OS-assigned TCP port)
//   2. UDP beacon is bound on an OS-assigned loopback port
//   3. Client sends a UDP "discover" query to the beacon
//   4. Beacon replies with the HTTP server's TCP port as a decimal string
//   5. Client connects via TCP to the discovered port and verifies a 200 OK
//
// Everything runs on 127.0.0.1 - no multicast, no elevated permissions,
// no hardcoded ports, safe to run in parallel with other test binaries.

#include "HttpPollServer.h"
#include "UdpSocket.h"
#include "TcpSocket.h"
#include "SocketFactory.h"
#include "test_helpers.h"
#include <atomic>
#include <string>
#include <thread>
#include <chrono>

using namespace aiSocks;
using namespace std::chrono_literals;

// Minimal HTTP server: answers every request with "200 OK / body: OK".
class DiscoveryTestServer : public HttpPollServer {
    public:
    std::atomic<bool> ready_{false};

    void waitReady() const {
        const auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!ready_.load(std::memory_order_acquire)
            && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
        REQUIRE_MSG(ready_.load(std::memory_order_acquire),
            "server readiness timed out");
    }

    explicit DiscoveryTestServer(const ServerBind& bind)
        : HttpPollServer(bind) {}

    protected:
    void onReady() override { ready_.store(true, std::memory_order_release); }

    void buildResponse(HttpClientState& state) override {
        state.dataBuf = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
        state.dataView = state.dataBuf;
    }
};

int main() {
    printf("=== UDP Discovery Integration Tests ===\n");
    fflush(stdout);

    // Test 1: UDP query returns HTTP port, client connects and gets 200 OK
    BEGIN_TEST(
        "Discovery: UDP query returns HTTP port, TCP client gets 200 OK");
    {
        // --- HTTP server ---
        DiscoveryTestServer httpServer(ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(httpServer.isValid());

        std::thread httpThread([&httpServer]() {
            httpServer.run(ClientLimit::Unlimited, Milliseconds{1});
        });
        httpServer.waitReady();

        const Port httpPort = httpServer.serverPort();
        REQUIRE(httpPort.value() != 0);

        // --- UDP beacon: bound before the beacon thread starts so the kernel
        //     queues any early datagrams in the socket's receive buffer. ---
        auto beaconResult = SocketFactory::createUdpServer(
            AddressFamily::IPv4, ServerBind{"127.0.0.1", Port{0}});
        REQUIRE(beaconResult.isSuccess());
        auto& beacon = beaconResult.value();
        REQUIRE(beacon.setReceiveTimeout(Milliseconds{500}));

        auto beaconEp = beacon.getLocalEndpoint();
        REQUIRE(beaconEp.isSuccess());
        const Port beaconPort = beaconEp.value().port;
        REQUIRE(beaconPort.value() != 0);

        // Beacon thread: receive one query, reply with the HTTP port number.
        // beacon is captured by reference; the join() below guarantees the
        // thread exits before beaconResult goes out of scope.
        std::thread beaconThread([&beacon, httpPort]() {
            char buf[64] = {};
            Endpoint from;
            int n = beacon.receiveFrom(buf, sizeof(buf) - 1, from);
            if (n > 0) {
                std::string reply = std::to_string(httpPort.value());
                beacon.sendTo(reply.data(), reply.size(), from);
            }
        });

        // --- UDP client: send discovery query ---
        auto clientUdpResult = SocketFactory::createUdpSocket();
        REQUIRE(clientUdpResult.isSuccess());
        auto& clientUdp = clientUdpResult.value();
        REQUIRE(clientUdp.setReceiveTimeout(Milliseconds{500}));

        const Endpoint beaconEndpoint{
            "127.0.0.1", beaconPort, AddressFamily::IPv4};
        const char query[] = "discover";
        int sent = clientUdp.sendTo(query, sizeof(query) - 1, beaconEndpoint);
        REQUIRE(sent == static_cast<int>(sizeof(query) - 1));

        // Receive the beacon's reply
        char replyBuf[64] = {};
        Endpoint replyFrom;
        int rn
            = clientUdp.receiveFrom(replyBuf, sizeof(replyBuf) - 1, replyFrom);
        REQUIRE(rn > 0);

        beaconThread.join();

        const Port discoveredPort{static_cast<uint16_t>(
            std::stoi(std::string(replyBuf, static_cast<size_t>(rn))))};
        REQUIRE(discoveredPort.value() == httpPort.value());

        // --- TCP: connect to discovered port, verify HTTP response ---
        auto tcpResult = SocketFactory::createTcpClient(
            AddressFamily::IPv4, ConnectArgs{"127.0.0.1", discoveredPort});
        REQUIRE(tcpResult.isSuccess());

        auto& tcp = tcpResult.value();
        REQUIRE(tcp.setReceiveTimeout(Milliseconds{1000}));

        const std::string req
            = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        REQUIRE(tcp.sendAll(req.data(), req.size()));

        // Read until EOF (Connection: close means server closes after response)
        std::string response;
        char buf[1024];
        int n;
        while ((n = tcp.receive(buf, sizeof(buf))) > 0)
            response.append(buf, static_cast<size_t>(n));

        REQUIRE(response.find("200 OK") != std::string::npos);

        g_serverSignalStop.store(true);
        httpThread.join();
        g_serverSignalStop.store(false);
    }

    return test_summary();
}
