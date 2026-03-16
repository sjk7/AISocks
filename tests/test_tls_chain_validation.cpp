#include "HttpsFileServer.h"
#include "TlsOpenSsl.h"
#include "test_helpers.h"

using namespace aiSocks;

void test_tls_chain_validation_requires_full_chain() {
    BEGIN_TEST(
        "TLS server rejects single-cert chain file when requireFullChain=true");

    const std::string root = __FILE__;
    const std::string marker = "/tests/test_tls_chain_validation.cpp";
    const size_t pos = root.rfind(marker);
    std::string repoRoot
        = (pos != std::string::npos) ? root.substr(0, pos) : ".";

    const std::string cert = repoRoot + "/tests/certs/test_cert.pem";
    const std::string key = repoRoot + "/tests/certs/test_key.pem";

    HttpFileServer::Config cfg;
    cfg.documentRoot = ".";
    cfg.indexFile = "index.html";

    TlsServerConfig tls;
    tls.certChainFile = cert;
    tls.privateKeyFile = key;
    tls.requireFullChain = true;

    HttpsFileServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
    REQUIRE(!server.tlsReady());
}

int main() {
    test_tls_chain_validation_requires_full_chain();
    return test_summary();
}
