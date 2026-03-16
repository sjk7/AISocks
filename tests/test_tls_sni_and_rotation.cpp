#include "HttpsFileServer.h"
#include "TlsOpenSsl.h"
#include "test_helpers.h"
#include "SocketFactory.h"

#include <chrono>
#include <fstream>
#include <thread>
#include <openssl/ssl.h>

using namespace aiSocks;

static std::string repoRootFromFile(const char* file) {
    std::string path = std::filesystem::path(file).generic_string();
    const std::string marker = "/tests/";
    const size_t pos = path.rfind(marker);
    if (pos != std::string::npos) return path.substr(0, pos);
    return ".";
}

void test_tls_sni_and_rotation() {
    BEGIN_TEST("test_tls_sni_and_rotation");

    // Prepare minimal docroot
    const std::string docRoot = "./test_tmp_docroot_sni";
    std::filesystem::create_directories(docRoot);
    std::ofstream out(docRoot + "/index.html");
    out << "hello";
    out.close();

    HttpFileServer::Config cfg;
    cfg.documentRoot = docRoot;
    cfg.indexFile = "index.html";

    const std::string repoRoot = repoRootFromFile(__FILE__);
    const std::string cert1 = repoRoot + "/tests/certs/test_cert.pem";
    const std::string key1 = repoRoot + "/tests/certs/test_key.pem";
    const std::string cert2 = repoRoot + "/tests/certs/test_cert_ipv6.pem";
    const std::string key2 = repoRoot + "/tests/certs/test_key_ipv6.pem";

    auto get_server_cert_subject
        = [&](const std::string& cert, const std::string& key) -> std::string {
        TlsServerConfig tls;
        tls.certChainFile = cert;
        tls.privateKeyFile = key;

        HttpsFileServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
        REQUIRE(server.tlsReady());

        std::thread serverThread(
            [&] { server.run(ClientLimit::Unlimited, Milliseconds{5}); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Create client TCP socket
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
        REQUIRE(
            sess->attachSocket(static_cast<int>(sock.getNativeHandle()), &err));

        // Set SNI to a distinct hostname
        const char* sniName = "example.test.local";
        SSL_set_tlsext_host_name(sess->nativeHandle(), sniName);
        sess->setConnectState();

        // Perform handshake
        bool ok = false;
        for (;;) {
            int r = sess->handshake();
            if (r == 1) {
                ok = true;
                break;
            }
            int e = sess->getLastErrorCode(r);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) continue;
            break;
        }

        std::string subj;
        if (ok) {
            subj = sess->getPeerCertificateSubject();
        }

        sock.close();
        server.requestStop();
        serverThread.join();
        return subj;
    };

    const std::string subj1 = get_server_cert_subject(cert1, key1);
    const std::string subj2 = get_server_cert_subject(cert2, key2);

    // Expect different cert subjects between the two cert files
    REQUIRE(!subj1.empty());
    REQUIRE(!subj2.empty());
    REQUIRE(subj1 != subj2);
}

int main() {
    test_tls_sni_and_rotation();
    return test_summary();
}
