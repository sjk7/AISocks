#include "HttpsPollServer.h"
#include "HttpClient.h"
#include "TlsOpenSsl.h"
#include "test_helpers.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <optional>
#include <filesystem>
#include <condition_variable>

#ifdef AISOCKS_ENABLE_TLS

using namespace aiSocks;

static std::string sourceRoot() {
    std::string path = std::filesystem::path(__FILE__).generic_string();
    const std::string marker = "/tests/test_tls_metrics.cpp";
    const size_t pos = path.rfind(marker);
    if (pos != std::string::npos) return path.substr(0, pos);
    return ".";
}

class TestTlsServer : public HttpsPollServer {
    public:
    explicit TestTlsServer(const ServerBind& bind,
        const TlsServerConfig& tls)
        : HttpsPollServer(bind, AddressFamily::IPv4, tls) {
        setHandleSignals(false);
    }

    void waitReady() {
        std::unique_lock<std::mutex> lk(readyMtx_);
        readyCv_.wait(lk, [this] { return ready_.load(); });
    }

    protected:
    void onReady() override {
        {
            std::unique_lock<std::mutex> lk(readyMtx_);
            ready_ = true;
        }
        readyCv_.notify_one();
    }

    void buildResponse(HttpClientState& s) override {
        s.responseBuf = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
        s.responseView = s.responseBuf;
    }

    private:
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;
};

void test_tls_metrics_initial_state() {
    BEGIN_TEST("test_tls_metrics_initial_state");

    // Create a server and verify metrics start at zero
    TlsServerConfig tlsCfg;
        const std::string root = sourceRoot();
        tlsCfg.certChainFile = root + "/tests/certs/test_cert.pem";
        tlsCfg.privateKeyFile = root + "/tests/certs/test_key.pem";
    ServerBind bind{"127.0.0.1", Port{0}};
    std::optional<TestTlsServer> serverOpt;
    serverOpt.emplace(bind, tlsCfg);
    auto& server = *serverOpt;
    REQUIRE(server.isValid());
    REQUIRE(server.tlsReady());

    const TlsMetrics& metrics = server.getTlsMetrics();
    
    // All counters should start at zero
    REQUIRE(metrics.handshakeSuccessCount == 0);
    REQUIRE(metrics.handshakeFailureCount == 0);
    REQUIRE(metrics.handshakeTimeoutCount == 0);
    REQUIRE(metrics.protocolDistribution.empty());
    REQUIRE(metrics.cipherDistribution.empty());
}

void test_tls_metrics_multiple_protocols() {
    BEGIN_TEST("test_tls_metrics_multiple_protocols");

    // This test verifies that the metrics structure can hold multiple
    // protocol/cipher entries
    TlsMetrics metrics;
    metrics.protocolDistribution["TLSv1.2"] = 5;
    metrics.protocolDistribution["TLSv1.3"] = 3;
    metrics.cipherDistribution["AES-256-GCM"] = 4;
    metrics.cipherDistribution["CHACHA20-POLY1305"] = 4;

    REQUIRE(metrics.protocolDistribution.size() == 2);
    REQUIRE(metrics.cipherDistribution.size() == 2);
    REQUIRE(metrics.protocolDistribution["TLSv1.2"] == 5);
    REQUIRE(metrics.protocolDistribution["TLSv1.3"] == 3);
    REQUIRE(metrics.cipherDistribution["AES-256-GCM"] == 4);
    REQUIRE(metrics.cipherDistribution["CHACHA20-POLY1305"] == 4);
}

void test_tls_metrics_handshake_collection() {
    BEGIN_TEST("test_tls_metrics_handshake_collection");

    // Create server with TLS enabled
    TlsServerConfig tlsCfg;
    tlsCfg.handshakeTimeoutMs = 5000;

        const std::string root = sourceRoot();
        tlsCfg.certChainFile = root + "/tests/certs/test_cert.pem";
        tlsCfg.privateKeyFile = root + "/tests/certs/test_key.pem";
    ServerBind bind{"127.0.0.1", Port{0}};
    std::optional<TestTlsServer> serverOpt;
    serverOpt.emplace(bind, tlsCfg);
    auto& server = *serverOpt;
    REQUIRE(server.isValid());
    REQUIRE(server.tlsReady());

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{10}); });
    server.waitReady();

    // Verify initial metrics
    const TlsMetrics& metricsStart = server.getTlsMetrics();
    REQUIRE(metricsStart.handshakeSuccessCount == 0);
    REQUIRE(metricsStart.handshakeFailureCount == 0);
    REQUIRE(metricsStart.handshakeTimeoutCount == 0);

    // Attempt HTTPS connections (may succeed or fail due to cert validation)
    {
        HttpClient::Options opts;
        opts.connectTimeout = Milliseconds{1000};
        opts.requestTimeout = Milliseconds{1000};
            opts.verifyCertificate = false;
        HttpClient client{opts};
        const std::string url = "https://127.0.0.1:"
            + std::to_string(server.serverPort().value()) + "/test";
        // Try multiple times to get at least one handshake attempt
        for (int i = 0; i < 2; ++i) {
            auto result = client.get(url);
            // Ignore result; we just care about metrics being recorded
        }
    }

    // Give it a moment to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    server.requestStop();
    serverThread.join();

    // Verify metrics were collected
    const TlsMetrics& metrics = server.getTlsMetrics();
    
    // Should have at least one handshake attempt (success or failure)
    const uint64_t totalAttempts = metrics.handshakeSuccessCount 
        + metrics.handshakeFailureCount 
        + metrics.handshakeTimeoutCount;
    REQUIRE(totalAttempts > 0);
        REQUIRE(metrics.handshakeSuccessCount > 0);
        REQUIRE(!metrics.protocolDistribution.empty());
        REQUIRE(!metrics.cipherDistribution.empty());
    
    // Print collected metrics for debugging
    std::fprintf(stderr,
        "Metrics: successes=%llu timeouts=%llu failures=%llu\n",
        static_cast<unsigned long long>(metrics.handshakeSuccessCount),
        static_cast<unsigned long long>(metrics.handshakeTimeoutCount),
        static_cast<unsigned long long>(metrics.handshakeFailureCount));
    
    for (const auto& [proto, count] : metrics.protocolDistribution) {
        std::fprintf(stderr, "  Protocol %s: %llu\n", proto.c_str(),
            static_cast<unsigned long long>(count));
    }
    
    for (const auto& [cipher, count] : metrics.cipherDistribution) {
        std::fprintf(stderr, "  Cipher %s: %llu\n", cipher.c_str(),
            static_cast<unsigned long long>(count));
    }
}

int main() {
    test_tls_metrics_initial_state();
    test_tls_metrics_multiple_protocols();
    test_tls_metrics_handshake_collection();
    return test_summary();
}

#else  // AISOCKS_ENABLE_TLS

int main() {
    fprintf(stderr, "TLS not enabled; skipping test\n");
    return 0;
}

#endif  // AISOCKS_ENABLE_TLS
