// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "TlsOpenSsl.h"

#ifdef AISOCKS_ENABLE_TLS

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <mutex>

namespace aiSocks {

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

bool TlsContext::configureVerifyPeer(
    bool verifyPeer, bool loadDefaultCaPaths, std::string* error) {
    if (!ctx_) {
        if (error) *error = "TLS context is not initialized";
        return false;
    }

    SSL_CTX_set_verify(
        ctx_, verifyPeer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);

    if (loadDefaultCaPaths && SSL_CTX_set_default_verify_paths(ctx_) != 1) {
        if (error) *error = TlsOpenSsl::lastErrorString();
        return false;
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
