#include "HttpsFileServer.h"
#include "PathHelper.h"
#include "TlsOpenSsl.h"
#include "test_helpers.h"
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace aiSocks;

static std::string repoRootFromFile() {
    const std::string source = PathHelper::normalizePath(__FILE__);
    const std::string marker = "/tests/test_tls_chain_validation.cpp";
    const size_t pos = source.rfind(marker);
    return (pos != std::string::npos) ? source.substr(0, pos) : ".";
}

static HttpFileServer::Config baseConfig() {
    HttpFileServer::Config cfg;
    cfg.documentRoot = ".";
    cfg.indexFile = "index.html";
    return cfg;
}

void test_tls_chain_validation_requires_full_chain() {
    BEGIN_TEST(
        "TLS server rejects single-cert chain file when requireFullChain=true");

    const std::string repoRoot = repoRootFromFile();

    const std::string cert = repoRoot + "/tests/certs/test_cert.pem";
    const std::string key = repoRoot + "/tests/certs/test_key.pem";

    HttpFileServer::Config cfg = baseConfig();

    TlsServerConfig tls;
    tls.certChainFile = cert;
    tls.privateKeyFile = key;
    tls.requireFullChain = true;

    HttpsFileServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
    REQUIRE(!server.tlsReady());
}

void test_tls_chain_validation_allows_single_cert_when_not_required() {
    BEGIN_TEST(
        "TLS server accepts single-cert chain file when requireFullChain=false");

    const std::string repoRoot = repoRootFromFile();
    const std::string cert = repoRoot + "/tests/certs/test_cert.pem";
    const std::string key = repoRoot + "/tests/certs/test_key.pem";

    HttpFileServer::Config cfg = baseConfig();

    TlsServerConfig tls;
    tls.certChainFile = cert;
    tls.privateKeyFile = key;
    tls.requireFullChain = false;

    HttpsFileServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
    REQUIRE(server.tlsReady());
    REQUIRE(server.isValid());
}

void test_tls_chain_validation_missing_chain_file_fails() {
    BEGIN_TEST("TLS server fails when chain file is missing");

    HttpFileServer::Config cfg = baseConfig();

    TlsServerConfig tls;
    tls.certChainFile = "/nonexistent/cert_chain.pem";
    tls.privateKeyFile = "/nonexistent/private_key.pem";
    tls.requireFullChain = false;

    HttpsFileServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
    REQUIRE(!server.tlsReady());
}

void test_tls_chain_validation_accepts_generated_full_chain_when_required() {
    BEGIN_TEST(
        "TLS server accepts generated 2-cert chain when requireFullChain=true");

    const std::string repoRoot = repoRootFromFile();
    const std::string cert = repoRoot + "/tests/certs/test_cert.pem";
    const std::string key = repoRoot + "/tests/certs/test_key.pem";
    const std::string chainPath = "test_generated_full_chain.pem";

    std::ifstream certIn(cert, std::ios::binary);
    REQUIRE(certIn.good());
    if (!certIn.good()) return;

    std::ostringstream certBuf;
    certBuf << certIn.rdbuf();
    const std::string certPem = certBuf.str();
    REQUIRE(!certPem.empty());
    if (certPem.empty()) return;

    {
        std::ofstream chainOut(chainPath, std::ios::binary | std::ios::trunc);
        REQUIRE(chainOut.good());
        if (!chainOut.good()) return;
        chainOut << certPem;
        chainOut << certPem;
    }

    HttpFileServer::Config cfg = baseConfig();

    TlsServerConfig tls;
    tls.certChainFile = chainPath;
    tls.privateKeyFile = key;
    tls.requireFullChain = true;

    HttpsFileServer server{ServerBind{"127.0.0.1", Port{0}}, cfg, tls};
    REQUIRE(server.tlsReady());
    REQUIRE(server.isValid());

    std::remove(chainPath.c_str());
}

int main() {
    test_tls_chain_validation_requires_full_chain();
    test_tls_chain_validation_allows_single_cert_when_not_required();
    test_tls_chain_validation_missing_chain_file_fails();
    test_tls_chain_validation_accepts_generated_full_chain_when_required();
    return test_summary();
}
