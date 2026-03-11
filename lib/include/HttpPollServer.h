// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifndef AISOCKS_HTTP_POLL_SERVER_H
#define AISOCKS_HTTP_POLL_SERVER_H

// ---------------------------------------------------------------------------
// HttpPollServer -- ServerBase specialisation that handles HTTP/1.x framing.
//
// Derive from this and implement buildResponse() to create an HTTP server.
// All connection management, request buffering, keep-alive negotiation, and
// response streaming are handled here.  The derived class only needs to
// inspect s.request, optionally read s.closeAfterSend, and fill s.response.
//
// Optional hooks (default: no-op):
//   onResponseBegin(s)  -- called when the first send of a response starts
//   onResponseSent(s)   -- called after the last byte of a response is sent
// ---------------------------------------------------------------------------

#include "CallIntervalTracker.h"
#include "HttpRequest.h"
#include "ServerBase.h"
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace aiSocks {

// ---------------------------------------------------------------------------
// Per-connection HTTP state
// ---------------------------------------------------------------------------
struct HttpClientState {
    std::string request;
    // Zero-copy response path: responseView is the active view of the response
    // data.  For static pre-built responses it points directly into the
    // server's long-lived std::string storage — no copy, no allocation.
    // For dynamic responses (error pages etc.) responseBuf owns the bytes
    // and responseView points into it.
    std::string_view responseView;
    std::string responseBuf;
    size_t sent{0};
    std::chrono::steady_clock::time_point startTime{};
    bool responseStarted{false}; // true once onResponseBegin has been called
    bool closeAfterSend{false}; // set by keep-alive negotiation
    // Tracks how far requestComplete() has already scanned so repeated
    // calls are O(n) total even when bytes arrive one at a time.
    size_t requestScanPos{0};
    // Cached parse result set by dispatchBuildResponse so that buildResponse
    // overrides in derived classes can skip a redundant HttpRequest::parse().
    // Cleared by resetAfterSend_() after the response is fully sent.
    std::optional<HttpRequest> parsedRequest;

    HttpClientState() : startTime(std::chrono::steady_clock::now()) {
        request.reserve(4096); // typical HTTP request fits in 4 KB
    }

    HttpClientState(const HttpClientState& other)
        : request(other.request)
        , responseView(other.responseView)
        , responseBuf(other.responseBuf)
        , sent(other.sent)
        , startTime(other.startTime)
        , responseStarted(other.responseStarted)
        , closeAfterSend(other.closeAfterSend)
        , requestScanPos(other.requestScanPos)
        , parsedRequest(other.parsedRequest) {
        // If view pointed into the original's responseBuf, redirect into ours.
        if (!responseBuf.empty()
            && other.responseView.data() == other.responseBuf.data())
            responseView = responseBuf;
    }

    HttpClientState(HttpClientState&& other) noexcept
        : request(std::move(other.request))
        , responseView(other.responseView)
        , responseBuf(std::move(other.responseBuf))
        , sent(other.sent)
        , startTime(other.startTime)
        , responseStarted(other.responseStarted)
        , closeAfterSend(other.closeAfterSend)
        , requestScanPos(other.requestScanPos)
        , parsedRequest(std::move(other.parsedRequest)) {
        // Fix up the view only if it was pointing into the moved-from buf.
        // After std::string move, responseBuf.data() == old
        // other.responseBuf.data() for heap-allocated strings, so the pointer
        // comparison is correct.
        if (!responseBuf.empty() && responseView.data() == responseBuf.data())
            responseView = responseBuf;
        other.responseView = {};
    }

    HttpClientState& operator=(const HttpClientState& other) {
        if (this == &other) return *this;
        request = other.request;
        responseView = other.responseView;
        responseBuf = other.responseBuf;
        sent = other.sent;
        startTime = other.startTime;
        responseStarted = other.responseStarted;
        closeAfterSend = other.closeAfterSend;
        requestScanPos = other.requestScanPos;
        parsedRequest = other.parsedRequest;
        // If view pointed into the original's responseBuf, redirect into ours.
        if (!responseBuf.empty()
            && other.responseView.data() == other.responseBuf.data())
            responseView = responseBuf;
        return *this;
    }

    HttpClientState& operator=(HttpClientState&& other) noexcept {
        if (this == &other) return *this;
        request = std::move(other.request);
        responseView = other.responseView;
        responseBuf = std::move(other.responseBuf);
        sent = other.sent;
        startTime = other.startTime;
        responseStarted = other.responseStarted;
        closeAfterSend = other.closeAfterSend;
        requestScanPos = other.requestScanPos;
        parsedRequest = std::move(other.parsedRequest);
        // Fix up the view only if it was pointing into the moved-from buf.
        if (!responseBuf.empty() && responseView.data() == responseBuf.data())
            responseView = responseBuf;
        other.responseView = {};
        return *this;
    }

    ~HttpClientState() = default;
};

// ---------------------------------------------------------------------------
// HttpPollServer
// ---------------------------------------------------------------------------
class HttpPollServer : public ServerBase<HttpClientState> {
    public:
    static constexpr size_t MAX_HEADER_SIZE = 8192;
    static constexpr int SLOWLORIS_TIMEOUT_MS = 5000; // 5 s

    explicit HttpPollServer(
        const ServerBind& bind, Result<TcpSocket>* result = nullptr)
        : ServerBase<HttpClientState>(bind, AddressFamily::IPv4, result)
        , bind_(bind) {}

    void run(ClientLimit maxClients = ClientLimit::Default,
        Milliseconds timeout = Milliseconds{-1});

    static void printBuildInfo();

    protected:
    ServerBind bind_;

    virtual void printStartupBanner();

    // -------------------------------------------------------------------------
    // Must override: fill s.response from s.request.
    // -------------------------------------------------------------------------
    virtual void buildResponse(HttpClientState& s) = 0;

    // -------------------------------------------------------------------------
    // Optional hooks (default: no-op)
    // -------------------------------------------------------------------------
    virtual void onResponseBegin(HttpClientState& /*s*/) {}
    virtual void onResponseSent(HttpClientState& /*s*/) {}

    ServerResult onError(TcpSocket& sock, HttpClientState& s) override;

    // -------------------------------------------------------------------------
    // HTTP helpers -- available to derived classes
    // -------------------------------------------------------------------------
    static bool isHttpRequest(const std::string& req);

    // Single-argument form: re-scans from the beginning every time.
    static bool requestComplete(const std::string& req);

    // Incremental form used internally: scanPos tracks the frontier.
    static bool requestComplete(const std::string& req, size_t& scanPos);

    static std::string makeResponse(const char* statusLine,
        const char* contentType, const std::string& body,
        bool keepAlive = true);

    void dispatchBuildResponse(HttpClientState& s);

    private:
    static constexpr size_t RECV_BUF_SIZE = 64 * 1024;

    ServerResult onReadable(TcpSocket& sock, HttpClientState& s) final;
    ServerResult onWritable(TcpSocket& sock, HttpClientState& s) final;
    ServerResult onIdle() override;
    void resetAfterSend_(HttpClientState& s);

    CallIntervalTracker tracker_;
};

} // namespace aiSocks

#endif // AISOCKS_HTTP_POLL_SERVER_H
