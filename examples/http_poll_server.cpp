// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Poll-driven HTTP/1.x server built on ServerBase.
// ServerBase owns all client socket/state bookkeeping; this file only
// contains HTTP framing logic.
//
// Compile with -DPERFORMANCE_TESTING to enable 100MB payload mode
// (for performance testing instead of normal HTTP server behavior).

#include "ServerBase.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

// Windows compatibility handled at compile time
// Windows builds should define their own mkstemp implementation

using namespace aiSocks;
using Microseconds = std::chrono::microseconds;

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------
namespace {

static constexpr size_t MAX_REQUEST_BYTES = 64 * 1024;
static constexpr size_t LARGE_RECV_BUFFER = 64 * 1024; // Larger receive buffer

bool isHttpRequest(const std::string& req) {
    return req.rfind("GET ", 0) == 0 || req.rfind("POST", 0) == 0
        || req.rfind("PUT ", 0) == 0 || req.rfind("HEAD", 0) == 0
        || req.rfind("DELE", 0) == 0 || req.rfind("OPTI", 0) == 0
        || req.rfind("PATC", 0) == 0;
}

bool requestComplete(const std::string& req) {
    return req.find("\r\n\r\n") != std::string::npos;
}

// Optimized response building with pre-allocation
std::string makeResponse(const char* statusLine, const char* contentType,
    const std::string& body, bool keepAlive = true) {
    std::string r;
    r.reserve(256 + body.size()); // Pre-allocate to avoid reallocations
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

} // namespace

// ---------------------------------------------------------------------------
// Per-connection state
// ---------------------------------------------------------------------------
using Clock = std::chrono::steady_clock;

static constexpr size_t PAYLOAD_BYTES = 100 * 1024 * 1024; // 100 MB

// Static 100MB buffer filled with 'A' characters at program startup
#ifdef PERFORMANCE_TESTING
static char preallocated_payload[PAYLOAD_BYTES];
static const bool payload_initialized = []() {
    std::fill_n(preallocated_payload, PAYLOAD_BYTES, 'A');
    return true;
}();

// Global payload file for sendfile - created once at startup
static int payload_fd = -1;
static const bool payload_file_initialized = []() {
    char template_path[] = "/tmp/aisocks_payload_XXXXXX";
    payload_fd = mkstemp(template_path);
    if (payload_fd != -1) {
        // Write 100MB of 'A' characters
        std::vector<char> buffer(64 * 1024, 'A'); // 64KB buffer
        size_t remaining = PAYLOAD_BYTES;
        while (remaining > 0) {
            size_t to_write = std::min(buffer.size(), remaining);
            ssize_t written = ::write(payload_fd, buffer.data(), to_write);
            if (written <= 0) break;
            remaining -= written;
        }
        // Unlink the file - it will be deleted when FD is closed
        unlink(template_path);
    }
    return payload_fd != -1;
}();
#endif // PERFORMANCE_TESTING

struct HttpClientState {
    std::string request;
    std::string response;
    size_t sent{0};
    Clock::time_point startTime{};
    bool timerStarted{false};
    bool closeAfterSend{false};
};

// ---------------------------------------------------------------------------
// HttpServer -- derives from ServerBase, handles HTTP framing only
// ---------------------------------------------------------------------------
class HttpServer : public ServerBase<HttpClientState> {
    public:
    explicit HttpServer(const ServerBind& bind)
        : ServerBase<HttpClientState>(bind) {
        std::cout << "Listening on " << bind.address << ":"
                  << static_cast<int>(bind.port) << "\n";
    }

    protected:
    void onError(TcpSocket& sock, HttpClientState& /*s*/) override {
        auto err = sock.getLastError();
        std::cout << "[error] poll error on client socket: code="
                  << static_cast<int>(err) << " msg=" << sock.getErrorMessage()
                  << "\n";
    }

    private:
    // Timing state for this instance
    Clock::time_point last_call_ = Clock::now();
    Clock::time_point last_print_ = Clock::now();
    std::vector<double> intervals_;
    int call_count_ = 0;
    bool first_output_done_ = false;

    protected:
    ServerResult onReadable(TcpSocket& sock, HttpClientState& s) override {
        char buf[LARGE_RECV_BUFFER]; // Use larger 64KB buffer
        for (;;) {
            int n = sock.receive(buf, sizeof(buf));
            if (n > 0) {
                touchClient(sock);
                s.request.append(buf, static_cast<size_t>(n));

                if (s.request.size() > MAX_REQUEST_BYTES) {
                    s.response = makeResponse("HTTP/1.1 413 Payload Too Large",
                        "text/plain; charset=utf-8", "Request too large.\n");
                    return ServerResult::KeepConnection;
                }

                if (s.response.empty() && requestComplete(s.request)) {
                    buildResponse(s);
                    setClientWritable(sock, true);
                    return onWritable(sock, s);
                }
            } else if (n == 0) {
                return ServerResult::Disconnect;
            } else {
                const auto err = sock.getLastError();
                if (err == SocketError::WouldBlock
                    || err == SocketError::Timeout) {
#if 0
                    std::cout << "[debug] read WouldBlock/Timeout on client: "
                              << sock.getErrorMessage() << "\n";
#endif
                    break; // no more data right now
                }
                if (err == SocketError::ConnectionReset) {
                    // Normal client disconnect - no action needed
                } else {
#if 0
                    std::cout << "[debug] read disconnect error: "
                              << static_cast<int>(err) << " "
                              << sock.getErrorMessage() << "\n";
#endif
                }
                return ServerResult::Disconnect;
            }
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, HttpClientState& s) override {
        if (s.response.empty())
            return ServerResult::KeepConnection; // nothing to send yet

#ifdef PERFORMANCE_TESTING
        if (!s.timerStarted) {
            s.startTime = Clock::now();
            s.timerStarted = true;
            std::cout << "Sending " << (PAYLOAD_BYTES / (1024 * 1024))
                      << " MB...\n";
        }
#endif

        // Use optimized send from ServerBase
        int sent = sendOptimized(
            sock, s.response.data() + s.sent, s.response.size() - s.sent);
        if (sent > 0) {
            touchClient(sock);
            s.sent += static_cast<size_t>(sent);
        } else {
            const auto err = sock.getLastError();
            if (err == SocketError::WouldBlock || err == SocketError::Timeout) {
#if 0
                std::cout << "[debug] write WouldBlock/Timeout on client: "
                          << sock.getErrorMessage() << "\n";
#endif
                return ServerResult::KeepConnection; // Try again later
            }
#if 0
            std::cout << "[debug] write disconnect error: "
                      << static_cast<int>(err) << " " << sock.getErrorMessage()
                      << "\n";
#endif
            return ServerResult::Disconnect; // Error
        }

        if (s.sent >= s.response.size()) {
#ifdef PERFORMANCE_TESTING
            const auto elapsed
                = std::chrono::duration<double>(Clock::now() - s.startTime)
                      .count();
            const double mb
                = static_cast<double>(PAYLOAD_BYTES) / (1024.0 * 1024.0);
            std::cout << "Sent " << mb << " MB in " << elapsed << " s  ("
                      << (mb / elapsed) << " MB/s)\n";
#endif
            // Reset for next request
            bool shouldClose = s.closeAfterSend;
            s.request.clear();
            s.response.clear();
            s.sent = 0;
            s.timerStarted = false;
            s.closeAfterSend = false;
            setClientWritable(sock, false);
            if (shouldClose) return ServerResult::Disconnect;
            return ServerResult::KeepConnection;
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onIdle() override {
        auto now = Clock::now();
        auto interval
            = std::chrono::duration<double, std::milli>(now - last_call_)
                  .count();
        last_call_ = now;

        intervals_.push_back(interval);
        call_count_++;

        // Print first output after 500ms, then every 60 seconds
        auto since_last_print
            = std::chrono::duration<double>(now - last_print_).count();
        auto print_interval = first_output_done_ ? 60.0 : 0.5;

        if (since_last_print >= print_interval) {
            if (!intervals_.empty()) {
                double sum = 0;
                for (double i : intervals_) {
                    sum += i;
                }
                double avg = sum / intervals_.size();
                std::cout << std::fixed << std::setprecision(1)
                          << "onIdle() called " << call_count_
                          << " times, avg interval: " << avg << "ms"
                          << "  clients: " << clientCount()
                          << "  peak: " << peakClientCount() << "\n";
            }

            // Reset for next period
            intervals_.clear();
            call_count_ = 0;
            last_print_ = now;
            first_output_done_ = true;
        }

        return ServerBase::onIdle();
    }

    static void buildResponse(HttpClientState& s) {
        bool http10 = s.request.find("HTTP/1.0") != std::string::npos;
        bool hasKeepAlive
            = s.request.find("Connection: keep-alive") != std::string::npos
            || s.request.find("connection: keep-alive") != std::string::npos
            || s.request.find("Connection: Keep-Alive") != std::string::npos;
        bool hasClose = s.request.find("Connection: close") != std::string::npos
            || s.request.find("connection: close") != std::string::npos
            || s.request.find("Connection: Close") != std::string::npos;
        // HTTP/1.1: keep-alive by default unless client says close
        // HTTP/1.0: close by default unless client explicitly requests
        // keep-alive
        bool keepAlive = http10 ? hasKeepAlive : !hasClose;
        s.closeAfterSend = !keepAlive;
        if (isHttpRequest(s.request)) {
#ifdef PERFORMANCE_TESTING
            // Performance testing mode - send 100MB payload
            s.response = makeResponse("HTTP/1.1 200 OK",
                "application/octet-stream",
                std::string(preallocated_payload, PAYLOAD_BYTES), keepAlive);
#else
            // Normal HTTP server behavior - simple response
            s.response
                = makeResponse("HTTP/1.1 200 OK", "text/html; charset=utf-8",
                    "<html><body><h1>Hello World!</h1><p>This is a normal HTTP "
                    "server response.</p></body></html>",
                    keepAlive);
#endif
        } else {
            s.response = makeResponse("HTTP/1.1 400 Bad Request",
                "text/plain; charset=utf-8",
                "Bad Request: this server only accepts HTTP requests.\n",
                keepAlive);
        }
    }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== Poll-Driven HTTP Server ===\n";
    try {
        HttpServer server(ServerBind{
            .address = "0.0.0.0",
            .port = Port{8080},
            .backlog = 1024,
        });
        server.run(0, Milliseconds{1}); // 1ms timeout
        std::cout << "\nShutting down cleanly.\n";
    } catch (const SocketException& e) {
        std::cerr << "Server error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
