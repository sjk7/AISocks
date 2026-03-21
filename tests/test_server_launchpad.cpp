// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpClient.h"
#include "HttpPollServer.h"
#include "PathHelper.h"
#include "Stopwatch.h"
#ifdef AISOCKS_ENABLE_TLS
#include "HttpsPollServer.h"
#endif
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

using namespace aiSocks;

#ifdef AISOCKS_ENABLE_TLS
static std::string sourceRoot() {
    std::string path = PathHelper::normalizePath(__FILE__);
    const std::string marker = "/tests/test_server_launchpad.cpp";
    const size_t pos = path.rfind(marker);
    if (pos != std::string::npos) return path.substr(0, pos);
    return ".";
}
#endif

class PlainLaunchpadServer : public HttpPollServer {
    public:
    explicit PlainLaunchpadServer(const ServerBind& bind)
        : HttpPollServer(bind) {
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
        s.dataBuf = makeResponse("HTTP/1.1 200 OK", "text/plain",
            "Hello from plain HTTP server_launchpad\n");
        s.dataView = s.dataBuf;
    }

    private:
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;
};

#ifdef AISOCKS_ENABLE_TLS
class TlsLaunchpadServer : public HttpsPollServer {
    public:
    TlsLaunchpadServer(const ServerBind& bind, const TlsServerConfig& tls)
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
        s.dataBuf = makeResponse("HTTP/1.1 200 OK", "text/plain",
            "Hello from HTTPS server_launchpad\n");
        s.dataView = s.dataBuf;
    }

    private:
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;
};
#endif

static void test_plain_server_launchpad_smoke() {
    BEGIN_TEST("server_launchpad plain HTTP smoke");
    Stopwatch totalTimer{"[timing] launchpad plain total"};

    std::optional<PlainLaunchpadServer> serverOpt;
    {
        Stopwatch constructionTimer{"[timing] launchpad plain construction"};
        serverOpt.emplace(ServerBind{"127.0.0.1", Port{0}});
    }
    auto& server = *serverOpt;
    REQUIRE(server.isValid());
    if (!server.isValid()) return;

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
    {
        Stopwatch readyTimer{"[timing] launchpad plain waitReady"};
        server.waitReady();
    }

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{1000};
    opts.requestTimeout = Milliseconds{1000};
    HttpClient client{opts};

    const std::string url = "http://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/";

    auto result = [&] {
        Stopwatch requestTimer{"[timing] launchpad plain client.get"};
        return client.get(url);
    }();

    {
        Stopwatch shutdownTimer{"[timing] launchpad plain shutdown"};
        server.requestStop();
        serverThread.join();
    }

    REQUIRE(result.isSuccess());
    if (!result.isSuccess()) return;
    REQUIRE(result.value().statusCode() == 200);
    REQUIRE(result.value().body().find("plain HTTP server_launchpad")
        != std::string_view::npos);
}

#ifdef AISOCKS_ENABLE_TLS
static void test_tls_server_launchpad_smoke() {
    BEGIN_TEST("server_launchpad HTTPS smoke");
    Stopwatch totalTimer{"[timing] launchpad tls total"};

    const std::string root = sourceRoot();
    const TlsServerConfig tls{root + "/tests/certs/test_cert.pem",
        root + "/tests/certs/test_key.pem"};

    std::optional<TlsLaunchpadServer> serverOpt;
    {
        Stopwatch constructionTimer{"[timing] launchpad tls construction"};
        serverOpt.emplace(ServerBind{"127.0.0.1", Port{0}}, tls);
    }
    auto& server = *serverOpt;
    REQUIRE(server.isValid());
    REQUIRE(server.tlsReady());
    if (!server.isValid() || !server.tlsReady()) return;

    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
    {
        Stopwatch readyTimer{"[timing] launchpad tls waitReady"};
        server.waitReady();
    }

    HttpClient::Options opts;
    opts.connectTimeout = Milliseconds{1000};
    opts.requestTimeout = Milliseconds{1000};
    opts.verifyCertificate = false;
    HttpClient client{opts};

    const std::string url = "https://127.0.0.1:"
        + std::to_string(server.serverPort().value()) + "/";

    auto result = [&] {
        Stopwatch requestTimer{"[timing] launchpad tls client.get"};
        return client.get(url);
    }();

    {
        Stopwatch shutdownTimer{"[timing] launchpad tls shutdown"};
        server.requestStop();
        serverThread.join();
    }

    REQUIRE(result.isSuccess());
    if (!result.isSuccess()) return;
    REQUIRE(result.value().statusCode() == 200);
    REQUIRE(result.value().body().find("HTTPS server_launchpad")
        != std::string_view::npos);
}
#endif

int main() {
    test_plain_server_launchpad_smoke();
#ifdef AISOCKS_ENABLE_TLS
    test_tls_server_launchpad_smoke();
#endif
    return test_summary();
}
