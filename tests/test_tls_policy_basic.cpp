// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com


#include "TlsOpenSsl.h"
#include "test_helpers.h"

#include <openssl/ssl.h>

#include <string>

using namespace aiSocks;

int main() {
    BEGIN_TEST("test_tls_policy_basic");

    std::string err;
    auto ctx = TlsContext::create(TlsContext::Mode::Server, &err);
    REQUIRE(ctx != nullptr);

    // Basic TLS1.2 cipher list application should succeed.
    std::string policyErr;
    bool ok = ctx->configureServerPolicy(
        "DEFAULT:!aNULL", "", TLS1_2_VERSION, 0, true, 2, &policyErr);
    REQUIRE(ok);

#if defined(SSL_CTX_set_ciphersuites)
    // If platform supports TLS1.3 ciphersuite control, ensure a common
    // TLS1.3 ciphersuite string applies without error.
    policyErr.clear();
    bool ok13 = ctx->configureServerPolicy(
        "", "TLS_AES_128_GCM_SHA256", 0, 0, false, -1, &policyErr);
    REQUIRE(ok13);
#endif

#if defined(SSL_CTX_set_security_level)
    // Security level should be accepted when explicitly set.
    policyErr.clear();
    bool oksec = ctx->configureServerPolicy("", "", 0, 0, false, 3, &policyErr);
    REQUIRE(oksec);
#endif

    return test_summary();
}
