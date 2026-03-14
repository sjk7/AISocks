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

#include "AccessLogger.h"
#include "CallIntervalTracker.h"
#include "HttpRequest.h"
#include "IpFilter.h"
#include "ServerBase.h"
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#ifdef AISOCKS_ENABLE_TLS
#include "TlsOpenSsl.h"
#endif

namespace aiSocks {

// ---------------------------------------------------------------------------
// Per-connection HTTP state
// ---------------------------------------------------------------------------
struct HttpClientState {
    std::string request;
    // Bytes for subsequent pipelined requests received while a response is
    // already being written.
    std::string queuedRequest;
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
    bool expectContinueSent{
        false}; // interim 100 Continue already sent for current request
    bool interimResponse{
        false}; // true when responseView contains interim 1xx (not final)
    // Tracks how far requestComplete() has already scanned so repeated
    // calls are O(n) total even when bytes arrive one at a time.
    size_t requestScanPos{0};
    // Cached parse result set by dispatchBuildResponse so that buildResponse
    // overrides in derived classes can skip a redundant HttpRequest::parse().
    // Cleared by resetAfterSend_() after the response is fully sent.
    // valid == false means no cached request yet.
    HttpRequest parsedRequest;
    // Remote address populated by HttpPollServer::onClientConnected().
    // Available in buildResponse() and onResponseSent().
    std::string peerAddress;

#ifdef AISOCKS_ENABLE_TLS
    // TLS state is present only in TLS-enabled builds.
    // Default is plain-socket behavior (handshake already satisfied).
    bool tlsHandshakeDone{true};
    bool tlsWantsWrite{false};
    std::shared_ptr<TlsSession> tlsSession;
#endif

    HttpClientState() : startTime(std::chrono::steady_clock::now()) {
        request.reserve(4096); // typical HTTP request fits in 4 KB
    }

    HttpClientState(const HttpClientState& other)
        : request(other.request)
        , queuedRequest(other.queuedRequest)
        , responseView(other.responseView)
        , responseBuf(other.responseBuf)
        , sent(other.sent)
        , startTime(other.startTime)
        , responseStarted(other.responseStarted)
        , closeAfterSend(other.closeAfterSend)
        , expectContinueSent(other.expectContinueSent)
        , interimResponse(other.interimResponse)
        , requestScanPos(other.requestScanPos)
        , parsedRequest(other.parsedRequest)
        , peerAddress(other.peerAddress)
#ifdef AISOCKS_ENABLE_TLS
        , tlsHandshakeDone(other.tlsHandshakeDone)
        , tlsWantsWrite(other.tlsWantsWrite)
        , tlsSession(other.tlsSession)
#endif
    {
        // If view pointed into the original's responseBuf, redirect into ours.
        if (!responseBuf.empty()
            && other.responseView.data() == other.responseBuf.data())
            responseView = responseBuf;
    }

    HttpClientState(HttpClientState&& other) noexcept
        : request(std::move(other.request))
        , queuedRequest(std::move(other.queuedRequest))
        , responseView(other.responseView)
        , responseBuf(std::move(other.responseBuf))
        , sent(other.sent)
        , startTime(other.startTime)
        , responseStarted(other.responseStarted)
        , closeAfterSend(other.closeAfterSend)
        , expectContinueSent(other.expectContinueSent)
        , interimResponse(other.interimResponse)
        , requestScanPos(other.requestScanPos)
        , parsedRequest(std::move(other.parsedRequest))
        , peerAddress(std::move(other.peerAddress))
#ifdef AISOCKS_ENABLE_TLS
        , tlsHandshakeDone(other.tlsHandshakeDone)
        , tlsWantsWrite(other.tlsWantsWrite)
        , tlsSession(std::move(other.tlsSession))
#endif
    {
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
        queuedRequest = other.queuedRequest;
        responseView = other.responseView;
        responseBuf = other.responseBuf;
        sent = other.sent;
        startTime = other.startTime;
        responseStarted = other.responseStarted;
        closeAfterSend = other.closeAfterSend;
        expectContinueSent = other.expectContinueSent;
        interimResponse = other.interimResponse;
        requestScanPos = other.requestScanPos;
        parsedRequest = other.parsedRequest;
        peerAddress = other.peerAddress;
#ifdef AISOCKS_ENABLE_TLS
        tlsHandshakeDone = other.tlsHandshakeDone;
        tlsWantsWrite = other.tlsWantsWrite;
        tlsSession = other.tlsSession;
#endif
        // If view pointed into the original's responseBuf, redirect into ours.
        if (!responseBuf.empty()
            && other.responseView.data() == other.responseBuf.data())
            responseView = responseBuf;
        return *this;
    }

    HttpClientState& operator=(HttpClientState&& other) noexcept {
        if (this == &other) return *this;
        request = std::move(other.request);
        queuedRequest = std::move(other.queuedRequest);
        responseView = other.responseView;
        responseBuf = std::move(other.responseBuf);
        sent = other.sent;
        startTime = other.startTime;
        responseStarted = other.responseStarted;
        closeAfterSend = other.closeAfterSend;
        expectContinueSent = other.expectContinueSent;
        interimResponse = other.interimResponse;
        requestScanPos = other.requestScanPos;
        parsedRequest = std::move(other.parsedRequest);
        peerAddress = std::move(other.peerAddress);
#ifdef AISOCKS_ENABLE_TLS
        tlsHandshakeDone = other.tlsHandshakeDone;
        tlsWantsWrite = other.tlsWantsWrite;
        tlsSession = std::move(other.tlsSession);
#endif
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
    static constexpr int SLOWLORIS_TIMEOUT_MS = 5000; // 5 s normal load
    // Under high client load the slowloris window is tightened so stalled
    // partial-request senders release their slots faster, reducing tail
    // latency.
    static constexpr int SLOWLORIS_TIMEOUT_MS_HIGH_LOAD = 1000; // 1 s
    static constexpr size_t SLOWLORIS_HIGH_LOAD_THRESHOLD = 64;
    // 32 KB is the sweet spot from benchmarking: large enough to read most
    // requests in a single syscall, small enough to keep per-loop stack
    // pressure manageable when many connections are active.
    static constexpr size_t RECV_BUF_SIZE = 32 * 1024;

    explicit HttpPollServer(
        const ServerBind& bind, Result<TcpSocket>* result = nullptr)
        : ServerBase<HttpClientState>(bind, AddressFamily::IPv4, result)
        , bind_(bind) {}

    HttpPollServer(const ServerBind& bind, AddressFamily family,
        Result<TcpSocket>* result = nullptr)
        : ServerBase<HttpClientState>(bind, family, result), bind_(bind) {}

    // ---- security / observability hooks ---------------------------------

    // Attach an IpFilter.  The server does NOT take ownership; the caller
    // must ensure the filter outlives the server.  Pass nullptr to disable.
    void setIpFilter(IpFilter* filter) { ipFilter_ = filter; }

    // Attach an AccessLogger.  Same lifetime contract as setIpFilter().
    void setAccessLogger(AccessLogger* logger) { accessLogger_ = logger; }

    // Override the Slowloris timeout for testing (default:
    // SLOWLORIS_TIMEOUT_MS)
    void setSlowlorisTimeout(int ms) { slowlorisTimeoutMs_ = ms; }

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
    // onResponseSent: base implementation writes to accessLogger_ (if set).
    // Derived classes that override this should call
    // HttpPollServer::onResponseSent(s) to preserve access logging.
    virtual void onResponseSent(HttpClientState& s);

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

    // Convenience helpers for common responses.
    // These auto-apply Connection based on s.closeAfterSend and populate both
    // responseBuf and responseView.
    static void setAutoResponse(HttpClientState& s, const char* statusLine,
        const char* contentType, const std::string& body) {
        s.responseBuf
            = makeResponse(statusLine, contentType, body, !s.closeAfterSend);
        s.responseView = s.responseBuf;
    }

    static void respondText(HttpClientState& s, const std::string& body,
        const char* statusLine = "HTTP/1.1 200 OK",
        const char* contentType = "text/plain; charset=utf-8") {
        setAutoResponse(s, statusLine, contentType, body);
    }

    static void respondJson(HttpClientState& s, const std::string& body,
        const char* statusLine = "HTTP/1.1 200 OK") {
        setAutoResponse(s, statusLine, "application/json; charset=utf-8", body);
    }

    void dispatchBuildResponse(HttpClientState& s);

    protected:
    // Override points — called by the infrastructure hooks.
    void onClientConnected(TcpSocket& sock, HttpClientState& s) override;
    bool onAcceptFilter(const std::string& peerAddress) override;

    // TLS extension points. Defaults preserve current plain-socket behavior.
    virtual bool isTlsMode(const HttpClientState& /*s*/) const { return false; }
    virtual void onTlsClientConnected(TcpSocket& /*sock*/, HttpClientState& s) {
#ifdef AISOCKS_ENABLE_TLS
        s.tlsHandshakeDone = true;
        s.tlsWantsWrite = false;
#else
        (void)s;
#endif
    }
    virtual ServerResult doTlsHandshakeStep(
        TcpSocket& /*sock*/, HttpClientState& s) {
#ifdef AISOCKS_ENABLE_TLS
        s.tlsHandshakeDone = true;
        s.tlsWantsWrite = false;
#else
        (void)s;
#endif
        return ServerResult::KeepConnection;
    }
    virtual int tlsRead(
        TcpSocket& sock, HttpClientState& /*s*/, void* buf, size_t len) {
        return sock.receive(buf, len);
    }
    virtual int tlsWrite(
        TcpSocket& sock, HttpClientState& /*s*/, const char* data, size_t len) {
        return sock.sendChunked(data, len);
    }

    private:
    IpFilter* ipFilter_{nullptr};
    AccessLogger* accessLogger_{nullptr};
    int slowlorisTimeoutMs_{SLOWLORIS_TIMEOUT_MS};

    ServerResult onReadable(TcpSocket& sock, HttpClientState& s) final;
    ServerResult onWritable(TcpSocket& sock, HttpClientState& s) final;
    ServerResult onIdle() override;
    void resetAfterSend_(HttpClientState& s);

    CallIntervalTracker tracker_;
};

} // namespace aiSocks

#endif // AISOCKS_HTTP_POLL_SERVER_H
