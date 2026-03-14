// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include <memory>
#include <string>

#ifdef AISOCKS_ENABLE_TLS

struct ssl_ctx_st;
struct ssl_st;

namespace aiSocks {

class TlsOpenSsl {
    public:
    static bool initialize();
    static std::string lastErrorString();
};

class TlsContext {
    public:
    enum class Mode {
        Server,
        Client,
    };

    TlsContext() = delete;
    ~TlsContext();

    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;
    TlsContext(TlsContext&& other) noexcept;
    TlsContext& operator=(TlsContext&& other) noexcept;

    static std::unique_ptr<TlsContext> create(
        Mode mode, std::string* error = nullptr);

    bool loadCertificateChain(const std::string& certPemPath,
        const std::string& keyPemPath, std::string* error = nullptr);

    bool configureVerifyPeer(bool verifyPeer, bool loadDefaultCaPaths,
        const std::string& caFile = {}, const std::string& caDir = {},
        std::string* error = nullptr);

    ssl_ctx_st* nativeHandle() const noexcept { return ctx_; }
    Mode mode() const noexcept { return mode_; }

    private:
    explicit TlsContext(ssl_ctx_st* ctx, Mode mode) noexcept
        : ctx_(ctx), mode_(mode) {}

    ssl_ctx_st* ctx_{nullptr};
    Mode mode_{Mode::Client};
};

class TlsSession {
    public:
    TlsSession() = delete;
    ~TlsSession();

    TlsSession(const TlsSession&) = delete;
    TlsSession& operator=(const TlsSession&) = delete;
    TlsSession(TlsSession&& other) noexcept;
    TlsSession& operator=(TlsSession&& other) noexcept;

    static std::unique_ptr<TlsSession> create(
        ssl_ctx_st* ctx, std::string* error = nullptr);

    bool attachSocket(int fd, std::string* error = nullptr);

    void setAcceptState() noexcept;
    void setConnectState() noexcept;

    int handshake();
    int read(void* dst, int size);
    int write(const void* src, int size);

    int getLastErrorCode(int ioResult) const;

    ssl_st* nativeHandle() const noexcept { return ssl_; }

    private:
    explicit TlsSession(ssl_st* ssl) noexcept : ssl_(ssl) {}

    ssl_st* ssl_{nullptr};
};

} // namespace aiSocks

#endif // AISOCKS_ENABLE_TLS
