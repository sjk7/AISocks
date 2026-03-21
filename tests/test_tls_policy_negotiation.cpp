// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "HttpsFileServer.h"
#include "PathHelper.h"
#include "TlsOpenSsl.h"
#include "test_helpers.h"
#include "SocketFactory.h"

#include <chrono>
#include <thread>
#include <openssl/ssl.h>

using namespace aiSocks;

static std::string repoRootFromFile(const char* file) {
    std::string path = PathHelper::normalizePath(file);
    const std::string marker = "/tests/";
    const size_t pos = path.rfind(marker);
    if (pos != std::string::npos) return path.substr(0, pos);
    return ".";
}

// Perform a raw TLS client handshake and return negotiated protocol/cipher
static bool do_client_handshake_neg(int port, std::string& outProto,
    std::string& outCipher) {
    ConnectArgs args;
    args.address = "127.0.0.1";
    args.port = Port{static_cast<uint16_t>(port)};
    args.connectTimeout = Milliseconds{2000};
    auto cres = SocketFactory::createTcpClient(args);
    if (!cres.isSuccess()) return false;
    TcpSocket sock = std::move(cres.value());

    std::string err;
    auto ctx = TlsContext::create(TlsContext::Mode::Client, &err);
    if (!ctx) return false;
    auto sess = TlsSession::create(ctx->nativeHandle(), &err);
    if (!sess) return false;
    if (!sess->attachSocket(static_cast<int>(sock.getNativeHandle()), &err))
        return false;

    sess->setConnectState();
    for (;;) {
        int r = sess->handshake();
        if (r == 1) {
            const char* v = SSL_get_version(sess->nativeHandle());
            const char* c = SSL_get_cipher_name(sess->nativeHandle());
            outProto = v ? v : std::string();
            outCipher = c ? c : std::string();
            return true;
        }
        int e = sess->getLastErrorCode(r);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) continue;
        break;
    }
    return false;
}

void test_tls_policy_negotiation() {
    BEGIN_TEST("test_tls_policy_negotiation");

    const std::string repoRoot = repoRootFromFile(__FILE__);
    const std::string serverCert = repoRoot + "/tests/certs/test_cert.pem";
    const std::string serverKey = repoRoot + "/tests/certs/test_key.pem";

    // Configure server to only allow TLS1.2 and a specific TLS1.2 cipher.
    HttpFileServer::Config cfg;
    cfg.documentRoot = ".";

    TlsServerConfig tls;
    tls.certChainFile = serverCert;
    tls.privateKeyFile = serverKey;
    tls.minProtoVersion = TLS1_2_VERSION;
    tls.maxProtoVersion = TLS1_2_VERSION; // force TLS 1.2 for negotiation
    tls.tls12CipherList = "ECDHE-RSA-AES128-GCM-SHA256";

    HttpsFileServer server{ServerBind{"127.0.0.1", Port{0, ""}}, cfg, tls};
    REQUIRE(server.tlsReady());
    std::thread serverThread(
        [&] { server.run(ClientLimit::Unlimited, Milliseconds{1}); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string proto, cipher;
    bool ok = do_client_handshake_neg(server.serverPort().value(), proto, cipher);
    REQUIRE(ok);

    // Log what was negotiated for debugging before asserting.
    std::fprintf(stderr, "negotiated proto='%s' cipher='%s'\n",
        proto.c_str(), cipher.c_str());

    // Expect at least TLS 1.2 to be negotiated and a non-empty cipher.
    REQUIRE(proto.find("TLSv1.2") != std::string::npos
        || proto.find("TLSv1.3") != std::string::npos);
    REQUIRE(!cipher.empty());

    server.requestStop();
    serverThread.join();
}

int main() {
    test_tls_policy_negotiation();
    return test_summary();
}
