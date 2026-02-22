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
#include <vector>
#include <algorithm>

namespace aiSocks {

// ---------------------------------------------------------------------------
// Per-connection HTTP state
// ---------------------------------------------------------------------------
struct HttpClientState {
    std::string request;
    std::string response;
    size_t sent{0};
    std::chrono::steady_clock::time_point startTime{};
    bool responseStarted{false}; // true once onResponseBegin has been called
    bool closeAfterSend{false}; // set by keep-alive negotiation; derived
                                // class may override in buildResponse()
};

// ---------------------------------------------------------------------------
// HttpPollServer
// ---------------------------------------------------------------------------
class HttpPollServer : public ServerBase<HttpClientState> {
    public:
    explicit HttpPollServer(const ServerBind& bind)
        : ServerBase<HttpClientState>(bind) {}

    protected:
    // -------------------------------------------------------------------------
    // Must override: fill s.response from s.request.
    // s.closeAfterSend is already set according to HTTP/1.0 vs 1.1 keep-alive
    // rules — read it to pick the right "Connection:" header, and optionally
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
    void onError(TcpSocket& sock, HttpClientState& /*s*/) override {
        auto err = sock.getLastError();
        // Poll systems can report error events with SO_ERROR==0 for connection
        // resets or other conditions. This is not a real error and should not
        // be logged.
        if (err != SocketError::None) {
            std::cout << "[error] poll error on client socket: code="
                      << static_cast<int>(err)
                      << " msg=" << sock.getErrorMessage() << "\n";
        }
    }

    // -------------------------------------------------------------------------
    // HTTP helpers — available to derived classes
    // -------------------------------------------------------------------------

    static bool isHttpRequest(const std::string& req) {
        return req.rfind("GET ", 0) == 0 || req.rfind("POST", 0) == 0
            || req.rfind("PUT ", 0) == 0 || req.rfind("HEAD", 0) == 0
            || req.rfind("DELE", 0) == 0 || req.rfind("OPTI", 0) == 0
            || req.rfind("PATC", 0) == 0;
    }

    static bool requestComplete(const std::string& req) {
        return req.find("\r\n\r\n") != std::string::npos;
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
                    // Request too large — respond and close.
                    s.response = makeResponse("HTTP/1.1 413 Payload Too Large",
                        "text/plain; charset=utf-8", "Request too large.\n",
                        false);
                    s.closeAfterSend = true;
                    setClientWritable(sock, true);
                    return onWritable(sock, s);
                }

                if (s.response.empty() && requestComplete(s.request)) {
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
        if (s.response.empty()) return ServerResult::KeepConnection;

        if (!s.responseStarted) {
            s.responseStarted = true;
            onResponseBegin(s);
        }

        int sent = sendOptimized(
            sock, s.response.data() + s.sent, s.response.size() - s.sent);
        if (sent > 0) {
            touchClient(sock);
            s.sent += static_cast<size_t>(sent);
        } else {
            const auto err = sock.getLastError();
            if (err == SocketError::WouldBlock || err == SocketError::Timeout)
                return ServerResult::KeepConnection;
            return ServerResult::Disconnect;
        }

        if (s.sent >= s.response.size()) {
            onResponseSent(s);
            bool shouldClose = s.closeAfterSend;
            s.request.clear();
            s.response.clear();
            s.sent = 0;
            s.responseStarted = false;
            s.closeAfterSend = false;
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
        double print_interval = first_output_done_ ? 30.0 : 2.0;

        if (since_print >= print_interval) {
            if (!intervals_.empty()) {
                double sum = 0;
                for (double v : intervals_) sum += v;
                std::cout << std::fixed << std::setprecision(1)
                          << "onIdle() called " << call_count_
                          << " times, avg interval: "
                          << (sum / static_cast<double>(intervals_.size()))
                          << "ms  clients: " << clientCount()
                          << "  peak: " << peakClientCount() << "\n";
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
