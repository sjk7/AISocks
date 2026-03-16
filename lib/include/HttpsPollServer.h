// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include "HttpPollServer.h"

#ifdef AISOCKS_ENABLE_TLS

#include <openssl/ssl.h>
#include <memory>
#include <string>

namespace aiSocks {

struct TlsServerConfig {
    std::string certChainFile;
    std::string privateKeyFile;
    // Optional server policy controls
    int minProtoVersion{TLS1_2_VERSION};
    int maxProtoVersion{0}; // 0 == leave OpenSSL default
    std::string tls12CipherList; // OpenSSL cipher list format for TLS1.2
    std::string tls13CipherSuites; // OpenSSL 1.1.1+ TLS1.3 comma-separated list
    bool preferServerCiphers{true};
    // Handshake timeout in milliseconds (0 == disabled). Separate from
    // HTTP slowloris protection.
    int handshakeTimeoutMs{5000};
    TlsServerConfig() = default;
    TlsServerConfig(const std::string& cert, const std::string& key)
        : certChainFile(cert), privateKeyFile(key) {}
};

class HttpsPollServer : public HttpPollServer {
    public:
    explicit HttpsPollServer(const ServerBind& bind, const TlsServerConfig& tls,
        Result<TcpSocket>* result = nullptr)
        : HttpPollServer(bind, result) {
        initTls_(tls);
    }

    HttpsPollServer(const ServerBind& bind, AddressFamily family,
        const TlsServerConfig& tls, Result<TcpSocket>* result = nullptr)
        : HttpPollServer(bind, family, result) {
        initTls_(tls);
    }

    bool tlsReady() const noexcept { return tlsContext_ != nullptr; }
    const std::string& tlsInitError() const noexcept { return tlsInitError_; }

    protected:
    bool isTlsMode(const HttpClientState& /*s*/) const override {
        return tlsContext_ != nullptr;
    }

    void onTlsClientConnected(TcpSocket& sock, HttpClientState& s) override {
        if (!tlsContext_) return;

        std::string err;
        auto session = TlsSession::create(tlsContext_->nativeHandle(), &err);
        if (!session) return;

        if (!session->attachSocket(
                static_cast<int>(sock.getNativeHandle()), &err)) {
            return;
        }

        session->setAcceptState();
        s.tlsSession = std::move(session);
        s.tlsHandshakeDone = false;
        s.tlsWantsWrite = false;
        // Start handshake timer independent of HTTP slowloris timer.
        s.startTime = std::chrono::steady_clock::now();
    }

    ServerResult doTlsHandshakeStep(
        TcpSocket& /*sock*/, HttpClientState& s) override {
        if (!s.tlsSession) return ServerResult::Disconnect;

        const int r = s.tlsSession->handshake();
        if (r == 1) {
            s.tlsHandshakeDone = true;
            s.tlsWantsWrite = false;
            return ServerResult::KeepConnection;
        }

        const int e = s.tlsSession->getLastErrorCode(r);
        if (e == SSL_ERROR_WANT_READ) {
            s.tlsWantsWrite = false;
            return ServerResult::KeepConnection;
        }
        if (e == SSL_ERROR_WANT_WRITE) {
            s.tlsWantsWrite = true;
            return ServerResult::KeepConnection;
        }

        // Check handshake timeout (separate from HTTP header timeout).
        if (tlsHandshakeTimeoutMs_ > 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed
                = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - s.startTime)
                      .count();
            if (elapsed > tlsHandshakeTimeoutMs_) {
                // Log detailed OpenSSL error if available.
                const std::string opensslErr = TlsOpenSsl::lastErrorString();
                std::fprintf(stderr,
                    "[tls] handshake timeout after %lld ms sslErr=%s\n",
                    static_cast<long long>(elapsed),
                    opensslErr.empty() ? "<empty>" : opensslErr.c_str());
                return ServerResult::Disconnect;
            }
        }

        // Non-retry TLS error -> disconnect. Surface OpenSSL details to logs.
        {
            const std::string opensslErr = TlsOpenSsl::lastErrorString();
            std::fprintf(stderr,
                "[tls] handshake failed sslErr=%s sslCode=%d\n",
                opensslErr.empty() ? "<empty>" : opensslErr.c_str(), e);
        }
        return ServerResult::Disconnect;
    }

    int tlsRead(TcpSocket& /*sock*/, HttpClientState& s, void* buf,
        size_t len) override {
        if (!s.tlsSession) return -1;
        return s.tlsSession->read(buf, static_cast<int>(len));
    }

    int tlsWrite(TcpSocket& /*sock*/, HttpClientState& s, const char* data,
        size_t len) override {
        if (!s.tlsSession) return 0;
        return s.tlsSession->write(data, static_cast<int>(len));
    }

    private:
    void initTls_(const TlsServerConfig& tls) {
        tlsInitError_.clear();

        if (tls.certChainFile.empty() || tls.privateKeyFile.empty()) {
            tlsInitError_
                = "TLS certChainFile/privateKeyFile must be non-empty";
            return;
        }

        auto ctx = TlsContext::create(TlsContext::Mode::Server, &tlsInitError_);
        if (!ctx) return;

        if (!ctx->loadCertificateChain(
                tls.certChainFile, tls.privateKeyFile, &tlsInitError_)) {
            return;
        }

        // Apply optional server policy (ciphers, proto range, server prefs).
        {
            std::string policyErr;
            if (!ctx->configureServerPolicy(tls.tls12CipherList,
                    tls.tls13CipherSuites, tls.minProtoVersion,
                    tls.maxProtoVersion, tls.preferServerCiphers, &policyErr)) {
                tlsInitError_ = policyErr.empty()
                    ? "configureServerPolicy failed"
                    : policyErr;
                return;
            }
        }

        tlsHandshakeTimeoutMs_ = tls.handshakeTimeoutMs;

        tlsContext_ = std::move(ctx);
    }

    std::unique_ptr<TlsContext> tlsContext_;
    std::string tlsInitError_;
    int tlsHandshakeTimeoutMs_{5000};
};

} // namespace aiSocks

#endif // AISOCKS_ENABLE_TLS
