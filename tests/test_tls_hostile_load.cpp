// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpsPollServer.h"
#include "HttpClient.h"
#include "PathHelper.h"
#include "SocketFactory.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using namespace aiSocks;

#ifdef AISOCKS_ENABLE_TLS

static std::string sourceRoot() {
    std::string path = PathHelper::normalizePath(__FILE__);
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
        const bool ready = readyCv_.wait_for(
            lk, std::chrono::seconds{2}, [this] { return ready_.load(); });
        REQUIRE_MSG(ready, "server readiness timed out");
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
        s.dataBuf = makeResponse(
            "HTTP/1.1 200 OK", "text/plain", "hostile-load-ok\n");
        s.dataView = s.dataBuf;
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

template <typename Predicate>
static bool waitUntil(Predicate&& predicate, Milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeout.count);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

void test_tls_hostile_stalled_handshake_load() {
    BEGIN_TEST("test_tls_hostile_stalled_handshake_load");

    const std::string root = sourceRoot();

    TlsServerConfig tls;
    tls.certChainFile = root + "/tests/certs/test_cert.pem";
    tls.privateKeyFile = root + "/tests/certs/test_key.pem";
    tls.handshakeTimeoutMs = 100;

    std::optional<TlsHostileLoadServer> serverOpt;
    serverOpt.emplace(ServerBind{"127.0.0.1", Port{0}}, tls);
    auto& server = *serverOpt;

    REQUIRE(server.isValid());
    REQUIRE(server.tlsReady());
    if (!server.isValid() || !server.tlsReady()) return;

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
    server.waitReady();

    const int port = static_cast<int>(server.serverPort().value());

    // Open many TCP clients that never send TLS handshake bytes.
    constexpr int hostileClients = 20;
    auto stalledClients = openStalledClients(port, hostileClients);
    REQUIRE(!stalledClients.empty());

    // Valid HTTPS client should still be served after hostile load.
    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{2000};
    opts.requestTimeout = Milliseconds{2000};
    opts.verifyCertificate = false;
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/";

    auto result = client.get(url);

    // Wait until at least one stalled handshake times out.
    // We check this AFTER the valid client request to ensure the server
    // had a chance to process the background timeouts while serving a real
    // client.
    const bool timeoutOccurred = waitUntil(
        [&server]() {
            return server.getTlsMetrics().handshakeTimeoutCount > 0;
        },
        Milliseconds{5000});
    REQUIRE_MSG(
        timeoutOccurred, "no handshake timeouts occurred during hostile load");

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
