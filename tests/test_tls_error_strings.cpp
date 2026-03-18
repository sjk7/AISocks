#include "TlsOpenSsl.h"
#include "PathHelper.h"
#include "test_helpers.h"

#include <string>

using namespace aiSocks;

static std::string repoRootFromFile(const char* file) {
    std::string path = PathHelper::normalizePath(file);
    const std::string marker = "/tests/";
    const size_t pos = path.rfind(marker);
    if (pos != std::string::npos) return path.substr(0, pos);
    return ".";
}

void test_tls_error_strings() {
    BEGIN_TEST("test_tls_error_strings");

    const std::string repoRoot = repoRootFromFile(__FILE__);
    const std::string cert = repoRoot + "/tests/certs/test_cert.pem";
    const std::string key = repoRoot + "/tests/certs/test_key.pem";

    // Happy path: create context and load real cert/key -> error strings empty
    {
        std::string err;
        auto ctx = TlsContext::create(TlsContext::Mode::Server, &err);
        REQUIRE(ctx);
        REQUIRE(err.empty());

        std::string loadErr;
        bool ok = ctx->loadCertificateChain(cert, key, &loadErr);
        REQUIRE(ok);
        REQUIRE(loadErr.empty());
    }

    // Sad path: loading a missing cert file returns false and non-empty error
    {
        std::string err;
        auto ctx = TlsContext::create(TlsContext::Mode::Server, &err);
        REQUIRE(ctx);

        std::string badErr;
        bool ok = ctx->loadCertificateChain(
            "/nonexistent/cert.pem", "/nonexistent/key.pem", &badErr);
        REQUIRE(!ok);
        REQUIRE(!badErr.empty());
    }

    // Sad path: verify peer with missing CA file should fail with descriptive
    // error
    {
        std::string err;
        auto ctx = TlsContext::create(TlsContext::Mode::Server, &err);
        REQUIRE(ctx);

        std::string verErr;
        bool ok = ctx->configureVerifyPeer(
            true, false, "/nonexistent/ca.pem", "", false, -1, &verErr);
        REQUIRE(!ok);
        REQUIRE(!verErr.empty());
    }
}

int main() {
    test_tls_error_strings();
    return test_summary();
}
