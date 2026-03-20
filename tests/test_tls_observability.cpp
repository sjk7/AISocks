// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "TlsOpenSsl.h"
#include "test_helpers.h"

using namespace aiSocks;

void test_tls_observability_cert_load_failure() {
    BEGIN_TEST("test_tls_observability_cert_load_failure");

    std::string err;
    auto ctx = TlsContext::create(TlsContext::Mode::Server, &err);
    REQUIRE(ctx);

    // Non-existent files should cause loadCertificateChain to fail and
    // populate the error string.
    std::string badCert = "nonexistent-cert.pem";
    std::string badKey = "nonexistent-key.pem";
    std::string lerr;
    bool ok = ctx->loadCertificateChain(badCert, badKey, &lerr);
    REQUIRE(!ok);
    REQUIRE(!lerr.empty());
}

void test_tls_observability_verify_peer_ca_missing() {
    BEGIN_TEST("test_tls_observability_verify_peer_ca_missing");

    std::string err;
    auto ctx = TlsContext::create(TlsContext::Mode::Server, &err);
    REQUIRE(ctx);

    // configureVerifyPeer should fail when a CA file is specified but does
    // not exist; error string should be set accordingly.
    std::string verr;
    bool ok = ctx->configureVerifyPeer(
        true, false, "no-such-ca.pem", "", true, -1, &verr);
    REQUIRE(!ok);
    REQUIRE(!verr.empty());
}

void test_tls_observability_alpn_behavior() {
    BEGIN_TEST("test_tls_observability_alpn_behavior");

    std::string err;
    auto ctx = TlsContext::create(TlsContext::Mode::Server, &err);
    REQUIRE(ctx);

    std::string aerr;
    std::vector<std::string> alpn = {"http/1.1"};
    bool ok = ctx->setAlpnProtocols(alpn, &aerr);

    // OpenSSL builds vary; ensure the API either succeeds or returns the
    // documented ALPN-unsupported error string.
    if (!ok) {
        REQUIRE(aerr == "ALPN unsupported on this OpenSSL build");
    }
}

int main() {
    test_tls_observability_cert_load_failure();
    test_tls_observability_verify_peer_ca_missing();
    test_tls_observability_alpn_behavior();
    return test_summary();
}
