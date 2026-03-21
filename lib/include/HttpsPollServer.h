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
#include "FileIO.h"

namespace aiSocks {

struct TlsServerConfig {
    std::string certChainFile;
    std::string privateKeyFile;
    enum class ClientAuthMode {
        None,
        Optional,
        Require,
    };
    // Optional server policy controls
    int minProtoVersion{TLS1_2_VERSION};
    int maxProtoVersion{0}; // 0 == leave OpenSSL default
    std::string tls12CipherList; // OpenSSL cipher list format for TLS1.2
    std::string tls13CipherSuites; // OpenSSL 1.1.1+ TLS1.3 comma-separated list
    bool preferServerCiphers{true};
    // Handshake timeout in milliseconds (0 == disabled). Separate from
    // HTTP slowloris protection.
    int handshakeTimeoutMs{5000};
    // ALPN protocols in server preference order (e.g. {"h2", "http/1.1"}).
    std::vector<std::string> alpnProtocols;
    // mTLS / client auth options
    ClientAuthMode clientAuth{ClientAuthMode::None};
    std::string caFile;
    std::string caDir;
    int verifyDepth{-1};
    // If true, require the configured certChainFile to contain a full
    // certificate chain (leaf + at least one issuer cert). Default false
    // to preserve current behavior.
    bool requireFullChain{false};
    // Optional OpenSSL security level (negative = leave OpenSSL defaults).
    int securityLevel{-1};
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

    // Get TLS metrics collected during server operation (handshake stats,
    // protocol/cipher distributions).
    const TlsMetrics& getTlsMetrics() const noexcept { return tlsMetrics_; }

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
            // Handshake completed; capture peer cert subject and ensure a
            // client certificate was presented when operating in Require
            // mTLS mode.
            const std::string peerSubj
                = s.tlsSession->getPeerCertificateSubject();
            if (tlsRequirePeerCert_ && peerSubj.empty()) {
                const std::string opensslErr = TlsOpenSsl::lastErrorString();
                std::fprintf(stderr,
                    "[tls] client cert required but none presented sslErr=%s\n",
                    opensslErr.empty() ? "<empty>" : opensslErr.c_str());
                return ServerResult::Disconnect;
            }

            // Log negotiated protocol and cipher for observability.
            const char* negotiatedProto = SSL_get_version(
                static_cast<SSL*>(s.tlsSession->nativeHandle()));
            const char* negotiatedCipher = SSL_get_cipher_name(
                static_cast<SSL*>(s.tlsSession->nativeHandle()));
            std::fprintf(stderr,
                "[tls] handshake success protocol=%s cipher=%s "
                "peer_subject=%s\n",
                negotiatedProto ? negotiatedProto : "<unknown>",
                negotiatedCipher ? negotiatedCipher : "<unknown>",
                peerSubj.empty() ? "<none>" : peerSubj.c_str());

            // Record metrics.
            ++tlsMetrics_.handshakeSuccessCount;
            if (negotiatedProto) {
                ++tlsMetrics_.protocolDistribution[negotiatedProto];
            }
            if (negotiatedCipher) {
                ++tlsMetrics_.cipherDistribution[negotiatedCipher];
            }

            s.tlsHandshakeDone = true;
            s.tlsWantsWrite = false;
            // Capture peer cert subject for application access.
            s.peerCertSubject = peerSubj;
            return ServerResult::KeepConnection;
        }

        const int e = s.tlsSession->getLastErrorCode(r);

        // Check handshake timeout (separate from HTTP header timeout).
        // This MUST run before the WANT_READ/WANT_WRITE early returns:
        // stalled clients that never send TLS bytes produce SSL_ERROR_WANT_READ
        // on every poll wake and would never reach the timeout check otherwise.
        if (tlsHandshakeTimeoutMs_ > 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed
                = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - s.startTime)
                      .count();
            if (elapsed > tlsHandshakeTimeoutMs_) {
                const std::string opensslErr = TlsOpenSsl::lastErrorString();
                std::fprintf(stderr,
                    "[tls] handshake timeout after %lld ms sslErr=%s\n",
                    static_cast<long long>(elapsed),
                    opensslErr.empty() ? "<empty>" : opensslErr.c_str());
                ++tlsMetrics_.handshakeTimeoutCount;
                return ServerResult::Disconnect;
            }
        }

        if (e == SSL_ERROR_WANT_READ) {
            s.tlsWantsWrite = false;
            return ServerResult::KeepConnection;
        }
        if (e == SSL_ERROR_WANT_WRITE) {
            s.tlsWantsWrite = true;
            return ServerResult::KeepConnection;
        }

        // Non-retry TLS error -> disconnect. Surface OpenSSL details to logs.
        {
            const std::string opensslErr = TlsOpenSsl::lastErrorString();
            std::fprintf(stderr,
                "[tls] handshake failed sslErr=%s sslCode=%d\n",
                opensslErr.empty() ? "<empty>" : opensslErr.c_str(), e);
            // Record failure metric.
            ++tlsMetrics_.handshakeFailureCount;
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

    // Sweep for stalled TLS handshakes on every idle tick.  Clients that
    // never send any TLS bytes are never readable in the poller, so
    // doTlsHandshakeStep() is never called for them — the only way to
    // enforce the handshake timeout for truly-stalled clients is here.
    ServerResult onIdle() override {
        if (tlsHandshakeTimeoutMs_ > 0) {
            const auto now = std::chrono::steady_clock::now();
            sweepClients([&](TcpSocket& /*sock*/, HttpClientState& s) {
                if (s.tlsHandshakeDone || !s.tlsSession)
                    return ServerResult::KeepConnection;
                const auto elapsed
                    = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - s.startTime)
                          .count();
                if (elapsed > tlsHandshakeTimeoutMs_) {
                    std::fprintf(stderr,
                        "[tls] idle sweep: handshake timeout after %lld ms\n",
                        static_cast<long long>(elapsed));
                    ++tlsMetrics_.handshakeTimeoutCount;
                    return ServerResult::Disconnect;
                }
                return ServerResult::KeepConnection;
            });
        }
        return HttpPollServer::onIdle();
    }

    private:
    // Whether server requires a client certificate (mTLS require mode).
    bool tlsRequirePeerCert_{false};
    void initTls_(const TlsServerConfig& tls) {
        tlsInitError_.clear();

        if (tls.certChainFile.empty() || tls.privateKeyFile.empty()) {
            tlsInitError_
                = "TLS certChainFile/privateKeyFile must be non-empty";
            std::fprintf(stderr,
                "[tls][init] failed reason=%s cert=%s key=%s\n",
                tlsInitError_.c_str(),
                tls.certChainFile.empty() ? "<empty>"
                                          : tls.certChainFile.c_str(),
                tls.privateKeyFile.empty() ? "<empty>"
                                           : tls.privateKeyFile.c_str());
            return;
        }

        auto ctx = TlsContext::create(TlsContext::Mode::Server, &tlsInitError_);
        if (!ctx) {
            std::fprintf(stderr, "[tls][init] create failed reason=%s\n",
                tlsInitError_.empty() ? "<empty>" : tlsInitError_.c_str());
            return;
        }

        if (!ctx->loadCertificateChain(
                tls.certChainFile, tls.privateKeyFile, &tlsInitError_)) {
            std::fprintf(stderr,
                "[tls][init] loadCertificateChain failed reason=%s cert=%s "
                "key=%s\n",
                tlsInitError_.empty() ? "<empty>" : tlsInitError_.c_str(),
                tls.certChainFile.c_str(), tls.privateKeyFile.c_str());
            return;
        }

        // Optional: enforce that the cert chain file contains more than one
        // certificate (leaf + issuer(s)) when configured to do so. This is a
        // cheap check that counts PEM certificates in the provided file and
        // helps detect incomplete deployments early.
        if (tls.requireFullChain) {
            int count = 0;
            File f;
            if (!f.open(tls.certChainFile.c_str(), "rb")) {
                tlsInitError_ = "failed to open cert chain file for "
                                "requireFullChain check";
                std::fprintf(stderr,
                    "[tls][init] requireFullChain failed cert=%s open failed\n",
                    tls.certChainFile.c_str());
                return;
            }
            const std::vector<char> contents = f.readAll();
            if (contents.empty()) {
                tlsInitError_ = "certificate chain file appears empty";
                std::fprintf(stderr,
                    "[tls][init] requireFullChain failed cert=%s empty\n",
                    tls.certChainFile.c_str());
                return;
            }
            const std::string s(contents.begin(), contents.end());
            size_t pos = 0;
            while (true) {
                pos = s.find("-----BEGIN CERTIFICATE-----", pos);
                if (pos == std::string::npos) break;
                ++count;
                pos += 24; // advance past the match
            }
            if (count < 2) {
                tlsInitError_
                    = "certificate chain file does not contain full chain";
                std::fprintf(stderr,
                    "[tls][init] requireFullChain failed cert=%s count=%d\n",
                    tls.certChainFile.c_str(), count);
                return;
            }
        }

        // Apply server policy using the new data-driven applyPolicy.
        {
            TlsPolicy policy;
            policy.minProtocol = tls.minProtoVersion;
            policy.maxProtocol = tls.maxProtoVersion;
            policy.tls12CipherList = tls.tls12CipherList;
            policy.tls13CipherSuites = tls.tls13CipherSuites;
            policy.preferServerCiphers = tls.preferServerCiphers;
            policy.securityLevel = tls.securityLevel;
            policy.alpnProtocols = tls.alpnProtocols;
            policy.verifyPeer
                = (tls.clientAuth != TlsServerConfig::ClientAuthMode::None);
            policy.failIfNoPeerCert
                = (tls.clientAuth == TlsServerConfig::ClientAuthMode::Require);
            policy.loadDefaultCaPaths
                = (tls.caFile.empty() && tls.caDir.empty());
            policy.caFile = tls.caFile;
            policy.caDir = tls.caDir;
            policy.verifyDepth = tls.verifyDepth;

            if (!ctx->applyPolicy(policy, &tlsInitError_)) {
                std::fprintf(stderr,
                    "[tls][init] applyPolicy failed reason=%s\n",
                    tlsInitError_.empty() ? "<empty>" : tlsInitError_.c_str());
                return;
            }

            if (tls.clientAuth == TlsServerConfig::ClientAuthMode::Require) {
                // Remember that the server requires a client cert so the
                // handshake completion step can explicitly enforce presence
                // of a peer certificate and disconnect if none was presented.
                tlsRequirePeerCert_ = true;
            }
        }

        tlsHandshakeTimeoutMs_ = tls.handshakeTimeoutMs;

        tlsContext_ = std::move(ctx);
    }

    std::unique_ptr<TlsContext> tlsContext_;
    std::string tlsInitError_;
    int tlsHandshakeTimeoutMs_{5000};
    TlsMetrics tlsMetrics_;
};

} // namespace aiSocks

#endif // AISOCKS_ENABLE_TLS
