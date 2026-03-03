// This is a personal academic project. Dear PVS-Studio, please check it.
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
//
// Usage:
//   class MyServer : public HttpPollServer {
//   public:
//       explicit MyServer(const ServerBind& b) : HttpPollServer(b) {
//           std::cout << "Listening on port " << (int)b.port << "\n";
//       }
//   protected:
//       void buildResponse(HttpClientState& s) override {
//           bool ka = !s.closeAfterSend;
//           s.response = makeResponse("HTTP/1.1 200 OK",
//               "text/plain; charset=utf-8", "Hello!\n", ka);
//       }
//   };
// ---------------------------------------------------------------------------

#include "ServerBase.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

namespace aiSocks {

// ---------------------------------------------------------------------------
// Per-connection HTTP state
// ---------------------------------------------------------------------------
// HTTP client state for tracking request/response data
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
        , requestScanPos(other.requestScanPos) {
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
        , requestScanPos(other.requestScanPos) {
        // After the move, fix up view if it was backed by the (now moved) buf.
        if (!responseBuf.empty()) responseView = responseBuf;
        other.responseView = {};
    }

    ~HttpClientState() = default;
};

// ---------------------------------------------------------------------------
// HttpPollServer
// ---------------------------------------------------------------------------
class HttpPollServer : public ServerBase<HttpClientState> {
    public:
    explicit HttpPollServer(const ServerBind& bind)
        : ServerBase<HttpClientState>(bind) {}

    // Run the server with startup/shutdown messages
    void run(ClientLimit maxClients = ClientLimit::Default,
             Milliseconds timeout = Milliseconds{-1}) {
        if (!this->isValid()) {
            return; // Server not valid, exit early
        }
        
        // Call base class run()
        ServerBase<HttpClientState>::run(maxClients, timeout);
        
        // Print shutdown message
        printf("\nServer stopped gracefully.\n");
    }
    
    // Print OS/Build info banner - call this from derived class before run()
    // Virtual so derived classes can customize the output
    virtual void printBuildInfo() const {
        printf("Built: %s %s  |  OS: %s  |  Build: %s\n", 
               __DATE__, __TIME__, buildOS(), buildKind());
    }

    protected:
    // Static helpers for OS/Build info
    static const char* buildOS() {
#if defined(__APPLE__)
        return "macOS";
#elif defined(__linux__)
        return "Linux";
#elif defined(_WIN32)
        return "Windows";
#else
        return "Unknown";
#endif
    }

    static const char* buildKind() {
#if defined(NDEBUG)
        return "Release";
#else
        return "Debug";
#endif
    }

    virtual void printStartupBanner() {
        printf("Built: %s %s  |  OS: %s  |  Build: %s\n", 
               __DATE__, __TIME__, buildOS(), buildKind());
    }
    // -------------------------------------------------------------------------
    // Must override: fill s.response from s.request.
    // s.closeAfterSend is already set according to HTTP/1.0 vs 1.1 keep-alive
    // rules -- read it to pick the right "Connection:" header, and optionally
    // override it (e.g. force close after an error response).
    // -------------------------------------------------------------------------
    virtual void buildResponse(HttpClientState& s) = 0;

    // -------------------------------------------------------------------------
    // Optional hooks
    // -------------------------------------------------------------------------

    // Called once, just before the first byte of s.response is sent.
    virtual void onResponseBegin(HttpClientState& /*s*/) {}

    // Called once, after the last byte of s.response has been flushed.
    virtual void onResponseSent(HttpClientState& /*s*/) {}

    // Default error handler: log and drop the connection.
    ServerResult onError(TcpSocket& sock, HttpClientState& /*s*/) override {
        auto err = sock.getLastError();
        // Poll systems can report error events with SO_ERROR==0 for connection
        // resets or other conditions. This is not a real error and should not
        // be logged.
        if (err != SocketError::None) {
            printf("[error] poll error on client socket: code=%d msg=%s\n",
                static_cast<int>(err), sock.getErrorMessage().c_str());
        }
        return ServerResult::Disconnect;
    }

    // -------------------------------------------------------------------------
    // HTTP helpers -- available to derived classes
    // -------------------------------------------------------------------------

    static bool isHttpRequest(const std::string& req) {
        return req.rfind("GET ", 0) == 0 || req.rfind("POST", 0) == 0
            || req.rfind("PUT ", 0) == 0 || req.rfind("HEAD", 0) == 0
            || req.rfind("DELE", 0) == 0 || req.rfind("OPTI", 0) == 0
            || req.rfind("PATC", 0) == 0;
    }

    // Single-argument form kept for backward compatibility with derived
    // classes that call it directly.  Re-scans from the beginning every
    // time; prefer the two-argument form for new code.
    static bool requestComplete(const std::string& req) {
        return req.find("\r\n\r\n") != std::string::npos;
    }

    // Incremental form used internally by onReadable().  scanPos tracks how
    // far we have already confirmed there is no header terminator, so each
    // call only inspects new bytes (plus a 3-byte overlap for split
    // \r\n\r\n sequences).  Total cost across all calls per request: O(n).
    static bool requestComplete(const std::string& req, size_t& scanPos) {
        const size_t start = scanPos >= 3 ? scanPos - 3 : 0;
        const size_t pos = req.find("\r\n\r\n", start);
        if (pos != std::string::npos) {
            return true;
        }
        // Advance the frontier; next call starts just behind the current end.
        scanPos = req.size() >= 3 ? req.size() - 3 : 0;
        return false;
    }

    // Build a complete HTTP response string with pre-allocated buffer.
    static std::string makeResponse(const char* statusLine,
        const char* contentType, const std::string& body,
        bool keepAlive = true) {
        std::string r;
        r.reserve(256 + body.size());
        r += statusLine;
        r += "\r\nContent-Type: ";
        r += contentType;
        r += "\r\nContent-Length: ";
        r += std::to_string(body.size());
        r += keepAlive ? "\r\nConnection: keep-alive\r\n\r\n"
                       : "\r\nConnection: close\r\n\r\n";
        r += body;
        return r;
    }

    // Parse keep-alive preference from s.request, set s.closeAfterSend, then
    // delegate to the virtual buildResponse().
    void dispatchBuildResponse(HttpClientState& s) {
        bool http10 = s.request.find("HTTP/1.0") != std::string::npos;
        bool hasKeepAlive
            = s.request.find("Connection: keep-alive") != std::string::npos
            || s.request.find("connection: keep-alive") != std::string::npos
            || s.request.find("Connection: Keep-Alive") != std::string::npos;
        bool hasClose = s.request.find("Connection: close") != std::string::npos
            || s.request.find("connection: close") != std::string::npos
            || s.request.find("Connection: Close") != std::string::npos;
        // HTTP/1.1: keep-alive by default unless client says close.
        // HTTP/1.0: close by default unless client explicitly requests
        // keep-alive.
        s.closeAfterSend = http10 ? !hasKeepAlive : hasClose;
        buildResponse(s);
    }

    private:
    // -------------------------------------------------------------------------
    // ServerBase overrides (final: HTTP framing is not further overridable)
    // -------------------------------------------------------------------------

    static constexpr size_t MAX_REQUEST_BYTES = 64 * 1024;
    static constexpr size_t RECV_BUF_SIZE = 64 * 1024;

    ServerResult onReadable(TcpSocket& sock, HttpClientState& s) final {
        char buf[RECV_BUF_SIZE];
        for (;;) {
            int n = sock.receive(buf, sizeof(buf));
            if (n > 0) {
                touchClient(sock);
                s.request.append(buf, static_cast<size_t>(n));

                if (s.request.size() > MAX_REQUEST_BYTES) {
                    // Request too large -- build into responseBuf, point view.
                    s.responseBuf
                        = makeResponse("HTTP/1.1 413 Payload Too Large",
                            "text/plain; charset=utf-8", "Request too large.\n",
                            false);
                    s.responseView = s.responseBuf;
                    s.closeAfterSend = true;
                    setClientWritable(sock, true);
                    return onWritable(sock, s);
                }

                if (s.responseView.empty()
                    && requestComplete(s.request, s.requestScanPos)) {
                    dispatchBuildResponse(s);
                    setClientWritable(sock, true);
                    return onWritable(sock, s);
                }
            } else if (n == 0) {
                return ServerResult::Disconnect;
            } else {
                const auto err = sock.getLastError();
                if (err == SocketError::WouldBlock
                    || err == SocketError::Timeout)
                    break; // no more data right now
                return ServerResult::Disconnect;
            }
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, HttpClientState& s) final {
        if (s.responseView.empty()) return ServerResult::KeepConnection;

        if (!s.responseStarted) {
            s.responseStarted = true;
            onResponseBegin(s);
        }

        int sent = sock.sendChunked(
            s.responseView.data() + s.sent, s.responseView.size() - s.sent);
        if (sent > 0) {
            touchClient(sock);
            s.sent += static_cast<size_t>(sent);
        } else {
            const auto err = sock.getLastError();
            if (err == SocketError::WouldBlock || err == SocketError::Timeout)
                return ServerResult::KeepConnection;
            return ServerResult::Disconnect;
        }

        if (s.sent >= s.responseView.size()) {
            onResponseSent(s);
            bool shouldClose = s.closeAfterSend;
            s.request.clear();
            s.responseView = {};
            s.responseBuf.clear();
            s.sent = 0;
            s.responseStarted = false;
            s.closeAfterSend = false;
            s.requestScanPos = 0;
            setClientWritable(sock, false);
            return shouldClose ? ServerResult::Disconnect
                               : ServerResult::KeepConnection;
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onIdle() override {
        auto now = std::chrono::steady_clock::now();
        auto interval
            = std::chrono::duration<double, std::milli>(now - last_call_)
                  .count();
        last_call_ = now;
        intervals_.push_back(interval);
        ++call_count_;

        auto since_print
            = std::chrono::duration<double>(now - last_print_).count();
        double print_interval = first_output_done_ ? 60.0 : 0.5;

        if (since_print >= print_interval) {
            if (!intervals_.empty()) {
                double sum = 0;
                for (double v : intervals_) sum += v;
                printf("onIdle() called %d times, avg interval: %.1fms  "
                       "clients: %zu  peak: %zu\n",
                    call_count_, sum / static_cast<double>(intervals_.size()),
                    static_cast<size_t>(clientCount()),
                    static_cast<size_t>(peakClientCount()));
            }
            intervals_.clear();
            call_count_ = 0;
            last_print_ = now;
            first_output_done_ = true;
        }

        return ServerBase::onIdle();
    }

    std::chrono::steady_clock::time_point last_call_
        = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_print_
        = std::chrono::steady_clock::now();
    std::vector<double> intervals_;
    int call_count_ = 0;
    bool first_output_done_ = false;
};

} // namespace aiSocks

#endif // AISOCKS_HTTP_POLL_SERVER_H
