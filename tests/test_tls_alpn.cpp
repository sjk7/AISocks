#include "HttpsFileServer.h"
#include "TlsOpenSsl.h"
#include "test_helpers.h"
#include "SocketFactory.h"

#include <chrono>
#include <fstream>
#include <thread>
#include <openssl/ssl.h>

using namespace aiSocks;

class TestHttpsFileServer : public HttpsFileServer {
    public:
    TestHttpsFileServer(
        const ServerBind& bind, const Config& cfg, const TlsServerConfig& tls)
        : HttpsFileServer(bind, cfg, tls) {
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

    private:
    std::atomic<bool> ready_{false};
    std::mutex readyMtx_;
    std::condition_variable readyCv_;
};

static std::string repoRootFromFile(const char* file) {
    std::string path = std::filesystem::path(file).generic_string();
    const std::string marker = "/tests/";
    const size_t pos = path.rfind(marker);
    if (pos != std::string::npos) return path.substr(0, pos);
    return ".";
}

void test_tls_alpn_selection() {
    BEGIN_TEST("TLS ALPN selection honors server preference and client offer");

    const std::string repoRoot = repoRootFromFile(__FILE__);
    const std::string cert = repoRoot + "/tests/certs/test_cert.pem";
    const std::string key = repoRoot + "/tests/certs/test_key.pem";

    // Minimal file server config
    HttpFileServer::Config cfg;
    cfg.documentRoot = ".";
    cfg.indexFile = "index.html";

    // create index
    std::ofstream out("index.html");
    out << "hello";
    out.close();

    TlsServerConfig tls;
    tls.certChainFile = cert;
    tls.privateKeyFile = key;
    // server prefers http/1.1 over h2
    tls.alpnProtocols = {"http/1.1", "h2"};

    TestHttpsFileServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
    REQUIRE(server.tlsReady());
    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
    server.waitReady();

    // Client offers http/1.1 first then h2; server preference should pick
    // http/1.1 (it's first in server list and present in client list).
    ConnectArgs args;
    args.address = "127.0.0.1";
    args.port = server.serverPort();
    args.connectTimeout = Milliseconds{2000};
    auto cres = SocketFactory::createTcpClient(args);
    REQUIRE(cres.isSuccess());
    TcpSocket sock = std::move(cres.value());

    std::string err;
    auto ctx = TlsContext::create(TlsContext::Mode::Client, &err);
    REQUIRE(ctx);
    auto sess = TlsSession::create(ctx->nativeHandle(), &err);
    REQUIRE(sess);
    REQUIRE(sess->attachSocket(static_cast<int>(sock.getNativeHandle()), &err));

    // client advertises http/1.1 then h2
    std::vector<std::string> clientAlpn = {"http/1.1", "h2"};
    if (!sess->setAlpnProtocols(clientAlpn, &err)) {
        // If ALPN unsupported, skip test
        server.requestStop();
        serverThread.join();
        REQUIRE_MSG(false, ("ALPN unsupported: " + err).c_str());
        return;
    }

    sess->setConnectState();
    int h = sess->handshake();
    if (h != 1) {
        // drive handshake until completion
        for (;;) {
            int r = sess->handshake();
            if (r == 1) break;
            int e = sess->getLastErrorCode(r);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) continue;
            REQUIRE_MSG(false, "TLS handshake failed");
            break;
        }
    }

    unsigned int len = 0;
    const unsigned char* sel
        = SSL_get0_alpn_selected(sess->nativeHandle(), &len);
    REQUIRE(sel != nullptr);
    std::string selected(reinterpret_cast<const char*>(sel), len);

    // server preferred http/1.1 first; since client offered it, it should be
    // selected.
    REQUIRE(selected == "http/1.1");

    sock.close();
    server.requestStop();
    serverThread.join();
}

int main() {
    test_tls_alpn_selection();
    return test_summary();
}
