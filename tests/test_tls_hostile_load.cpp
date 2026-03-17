#include "HttpsPollServer.h"
#include "HttpClient.h"
#include "SocketFactory.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using namespace aiSocks;

#ifdef AISOCKS_ENABLE_TLS

static std::string sourceRoot() {
    std::string path = std::filesystem::path(__FILE__).generic_string();
    const std::string marker = "/tests/test_tls_hostile_load.cpp";
    const size_t pos = path.rfind(marker);
    if (pos != std::string::npos) return path.substr(0, pos);
    return ".";
}

class TlsHostileLoadServer : public HttpsPollServer {
    public:
    TlsHostileLoadServer(const ServerBind& bind, const TlsServerConfig& tls)
        : HttpsPollServer(bind, tls) {
        setHandleSignals(false);
    }

    void waitReady() {
        std::unique_lock<std::mutex> lk(readyMtx_);
        readyCv_.wait(lk, [this] { return ready_.load(); });
    }

    protected:
    void onReady() override {
        {
            std::lock_guard<std::mutex> lk(readyMtx_);
            ready_ = true;
        }
        readyCv_.notify_all();
    }

    void buildResponse(HttpClientState& s) override {
        s.responseBuf = makeResponse(
            "HTTP/1.1 200 OK", "text/plain", "hostile-load-ok\n");
        s.responseView = s.responseBuf;
    }

    private:
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;
};

static std::vector<TcpSocket> openStalledClients(int port, int count) {
    std::vector<TcpSocket> clients;
    clients.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i) {
        ConnectArgs args;
        args.address = "127.0.0.1";
        args.port = Port{static_cast<uint16_t>(port)};
        args.connectTimeout = Milliseconds{500};

        auto cres = SocketFactory::createTcpClient(args);
        if (cres.isSuccess()) {
            clients.push_back(std::move(cres.value()));
        }
    }
    return clients;
}

void test_tls_hostile_stalled_handshake_load() {
    BEGIN_TEST("test_tls_hostile_stalled_handshake_load");

    const std::string root = sourceRoot();

    TlsServerConfig tls;
    tls.certChainFile = root + "/tests/certs/test_cert.pem";
    tls.privateKeyFile = root + "/tests/certs/test_key.pem";
    tls.handshakeTimeoutMs = 150;

    std::optional<TlsHostileLoadServer> serverOpt;
    serverOpt.emplace(ServerBind{"127.0.0.1", Port{0}}, tls);
    auto& server = *serverOpt;

    REQUIRE(server.isValid());
    REQUIRE(server.tlsReady());
    if (!server.isValid() || !server.tlsReady()) return;

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    const int port = static_cast<int>(server.serverPort().value());

    // Open many TCP clients that never send TLS handshake bytes.
    constexpr int hostileClients = 32;
    auto stalledClients = openStalledClients(port, hostileClients);
    REQUIRE(!stalledClients.empty());

    // Wait long enough for handshake timeout processing.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Valid HTTPS client should still be served after hostile load.
    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{1000};
    opts.requestTimeout = Milliseconds{1000};
    opts.verifyCertificate = false;
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/";

    auto result = client.get(url);

    stalledClients.clear();
    server.requestStop();
    serverThread.join();

    REQUIRE(result.isSuccess());
    if (result.isSuccess()) {
        REQUIRE(result.value().statusCode() == 200);
    }

    const TlsMetrics& metrics = server.getTlsMetrics();
    REQUIRE(metrics.handshakeTimeoutCount > 0);
    REQUIRE(metrics.handshakeSuccessCount > 0);
}

int main() {
    test_tls_hostile_stalled_handshake_load();
    return test_summary();
}

#else

int main() {
    std::fprintf(stderr, "TLS not enabled; skipping test\n");
    return 0;
}

#endif
