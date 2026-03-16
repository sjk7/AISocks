// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "TlsOpenSsl.h"
#include "PathHelper.h"

#ifdef AISOCKS_ENABLE_TLS

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/crypto.h>

#include <cstdlib>
#include <cstdio>
#include <mutex>

namespace aiSocks {

namespace {
    bool tlsDebugEnabled_() {
        const char* envName = "AISOCKS_TLS_DEBUG";
#ifdef _WIN32
        char* value = nullptr;
        size_t valueLen = 0;
        if (_dupenv_s(&value, &valueLen, envName) != 0 || value == nullptr) {
            return false;
        }

        const bool enabled = value[0] != '\0' && value[0] != '0';
        std::free(value);
        return enabled;
#else
        const char* value = std::getenv(envName);
        return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
    }

    void tlsDebugLog_(const std::string& msg) {
        if (!tlsDebugEnabled_()) return;
        std::fprintf(stderr, "[tls-debug] %s\n", msg.c_str());
    }
} // namespace

bool TlsOpenSsl::initialize() {
    static std::once_flag once;
    static bool initialized = true;
    std::call_once(once, [] {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
        initialized = (OPENSSL_init_ssl(0, nullptr) == 1);
#else
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        initialized = true;
#endif
    });
    return initialized;
}

std::string TlsOpenSsl::lastErrorString() {
    const unsigned long err = ERR_get_error();
    if (err == 0) return {};

    char buf[256] = {};
    ERR_error_string_n(err, buf, sizeof(buf));
    return std::string(buf);
}

TlsContext::~TlsContext() {
    if (ctx_) SSL_CTX_free(ctx_);
}

TlsContext::TlsContext(TlsContext&& other) noexcept
    : ctx_(other.ctx_), mode_(other.mode_) {
    other.ctx_ = nullptr;
}

TlsContext& TlsContext::operator=(TlsContext&& other) noexcept {
    if (this == &other) return *this;
    if (ctx_) SSL_CTX_free(ctx_);
    ctx_ = other.ctx_;
    mode_ = other.mode_;
    other.ctx_ = nullptr;
    return *this;
}

std::unique_ptr<TlsContext> TlsContext::create(Mode mode, std::string* error) {
    if (!TlsOpenSsl::initialize()) {
        if (error) *error = "OpenSSL initialization failed";
        return nullptr;
    }

    const SSL_METHOD* method
        = (mode == Mode::Server) ? TLS_server_method() : TLS_client_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) {
        if (error) *error = TlsOpenSsl::lastErrorString();
        return nullptr;
    }

    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    return std::unique_ptr<TlsContext>(new TlsContext(ctx, mode));
}

bool TlsContext::loadCertificateChain(const std::string& certPemPath,
    const std::string& keyPemPath, std::string* error) {
    if (!ctx_) {
        if (error) *error = "TLS context is not initialized";
        return false;
    }

    if (SSL_CTX_use_certificate_chain_file(ctx_, certPemPath.c_str()) != 1) {
        if (error) *error = TlsOpenSsl::lastErrorString();
        return false;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx_, keyPemPath.c_str(), SSL_FILETYPE_PEM)
        != 1) {
        if (error) *error = TlsOpenSsl::lastErrorString();
        return false;
    }

    if (SSL_CTX_check_private_key(ctx_) != 1) {
        if (error) *error = TlsOpenSsl::lastErrorString();
        return false;
    }

    return true;
}

bool TlsContext::configureVerifyPeer(bool verifyPeer, bool loadDefaultCaPaths,
    const std::string& caFile, const std::string& caDir,
    bool failIfNoPeerCert, int verifyDepth, std::string* error) {
    if (!ctx_) {
        if (error) *error = "TLS context is not initialized";
        return false;
    }

    int verifyMode = verifyPeer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE;
    if (verifyPeer && failIfNoPeerCert) verifyMode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
    SSL_CTX_set_verify(ctx_, verifyMode, nullptr);

    if (verifyPeer) {
        if (!caFile.empty() && !PathHelper::exists(caFile)) {
            if (error) *error = "CA file not found: " + caFile;
            return false;
        }
        if (!caDir.empty()) {
            if (!PathHelper::exists(caDir)) {
                if (error) *error = "CA directory not found: " + caDir;
                return false;
            }
            if (!PathHelper::isDirectory(caDir)) {
                if (error)
                    *error = "CA directory path is not a directory: " + caDir;
                return false;
            }
        }

        const char* caFileArg = caFile.empty() ? nullptr : caFile.c_str();
        const char* caDirArg = caDir.empty() ? nullptr : caDir.c_str();

        if (caFileArg || caDirArg) {
            if (SSL_CTX_load_verify_locations(ctx_, caFileArg, caDirArg) != 1) {
                tlsDebugLog_("SSL_CTX_load_verify_locations failed"
                             " caFile="
                    + (caFile.empty() ? std::string{"<empty>"} : caFile)
                    + " caDir="
                    + (caDir.empty() ? std::string{"<empty>"} : caDir));
                if (error) *error = TlsOpenSsl::lastErrorString();
                if (error) {
                    tlsDebugLog_("OpenSSL error: " + *error);
                }
                return false;
            }
        } else if (loadDefaultCaPaths
            && SSL_CTX_set_default_verify_paths(ctx_) != 1) {
            tlsDebugLog_("SSL_CTX_set_default_verify_paths failed");
            if (error) *error = TlsOpenSsl::lastErrorString();
            if (error) {
                tlsDebugLog_("OpenSSL error: " + *error);
            }
            return false;
        }

        if (verifyDepth >= 0) {
            SSL_CTX_set_verify_depth(ctx_, verifyDepth);
        }
    }

    return true;
}

std::string TlsSession::getPeerCertificateSubject() const {
    if (!ssl_) return {};
#if defined(OPENSSL_IS_BORINGSSL)
    // BoringSSL may not support X509_NAME_oneline; keep behavior simple.
    X509* cert = SSL_get_peer_certificate(ssl_);
    if (!cert) return {};
    char buf[256] = {};
    X509_NAME* name = X509_get_subject_name(cert);
    if (name) X509_NAME_oneline(name, buf, sizeof(buf));
    X509_free(cert);
    return std::string(buf);
#else
    X509* cert = SSL_get_peer_certificate(ssl_);
    if (!cert) return {};
    char* cstr = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
    std::string subj;
    if (cstr) {
        subj = std::string(cstr);
        OPENSSL_free(cstr);
    }
    X509_free(cert);
    return subj;
#endif
}

bool TlsContext::configureServerPolicy(const std::string& tls12CipherList,
    const std::string& tls13CipherSuites, int minProto, int maxProto,
    bool preferServerCiphers, std::string* error) {
    if (!ctx_) {
        if (error) *error = "TLS context is not initialized";
        return false;
    }

#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    if (minProto > 0) SSL_CTX_set_min_proto_version(ctx_, minProto);
    if (maxProto > 0) SSL_CTX_set_max_proto_version(ctx_, maxProto);
#else
    (void)minProto;
    (void)maxProto;
#endif

    if (!tls13CipherSuites.empty()) {
#if defined(SSL_CTX_set_ciphersuites)
        if (SSL_CTX_set_ciphersuites(ctx_, tls13CipherSuites.c_str()) != 1) {
            if (error) *error = TlsOpenSsl::lastErrorString();
            return false;
        }
#endif
    }

    if (!tls12CipherList.empty()) {
        if (SSL_CTX_set_cipher_list(ctx_, tls12CipherList.c_str()) != 1) {
            if (error) *error = TlsOpenSsl::lastErrorString();
            return false;
        }
    }

    if (preferServerCiphers) {
#ifdef SSL_OP_CIPHER_SERVER_PREFERENCE
        SSL_CTX_set_options(ctx_, SSL_OP_CIPHER_SERVER_PREFERENCE);
#endif
    }

    return true;
}

TlsSession::~TlsSession() {
    if (ssl_) SSL_free(ssl_);
}

TlsSession::TlsSession(TlsSession&& other) noexcept : ssl_(other.ssl_) {
    other.ssl_ = nullptr;
}

TlsSession& TlsSession::operator=(TlsSession&& other) noexcept {
    if (this == &other) return *this;
    if (ssl_) SSL_free(ssl_);
    ssl_ = other.ssl_;
    other.ssl_ = nullptr;
    return *this;
}

std::unique_ptr<TlsSession> TlsSession::create(
    ssl_ctx_st* ctx, std::string* error) {
    if (!ctx) {
        if (error) *error = "SSL_CTX is null";
        return nullptr;
    }

    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
        if (error) *error = TlsOpenSsl::lastErrorString();
        return nullptr;
    }

    return std::unique_ptr<TlsSession>(new TlsSession(ssl));
}

bool TlsSession::attachSocket(int fd, std::string* error) {
    if (!ssl_) {
        if (error) *error = "SSL session is not initialized";
        return false;
    }

    if (SSL_set_fd(ssl_, fd) != 1) {
        if (error) *error = TlsOpenSsl::lastErrorString();
        return false;
    }

    return true;
}

void TlsSession::setAcceptState() noexcept {
    if (ssl_) SSL_set_accept_state(ssl_);
}

void TlsSession::setConnectState() noexcept {
    if (ssl_) SSL_set_connect_state(ssl_);
}

int TlsSession::handshake() {
    if (!ssl_) return -1;
    return SSL_do_handshake(ssl_);
}

int TlsSession::read(void* dst, int size) {
    if (!ssl_) return -1;
    return SSL_read(ssl_, dst, size);
}

int TlsSession::write(const void* src, int size) {
    if (!ssl_) return -1;
    return SSL_write(ssl_, src, size);
}

int TlsSession::getLastErrorCode(int ioResult) const {
    if (!ssl_) return SSL_ERROR_SSL;
    return SSL_get_error(ssl_, ioResult);
}

} // namespace aiSocks

#endif // AISOCKS_ENABLE_TLS
