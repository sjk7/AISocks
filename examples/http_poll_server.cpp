// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Poll-driven HTTP/1.x server built on ServerBase.
// ServerBase owns all client socket/state bookkeeping; this file only
// contains HTTP framing logic.

#include "ServerBase.h"
#include <chrono>
#include <iostream>
#include <string>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define mkstemp(template) _mktemp_s(template, strlen(template) + 1)
#endif

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
std::string makeResponse(
    const char* statusLine, const char* contentType, const std::string& body) {
    std::string r;
    r.reserve(256 + body.size()); // Pre-allocate to avoid reallocations
    r += statusLine;
    r += "\r\nContent-Type: ";
    r += contentType;
    r += "\r\nContent-Length: ";
    r += std::to_string(body.size());
    r += "\r\nConnection: keep-alive\r\n\r\n";
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

struct HttpClientState {
    std::string request;
    std::string response;
    size_t sent{0};
    Clock::time_point startTime{};
    bool timerStarted{false};
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
    ServerResult onReadable(TcpSocket& sock, HttpClientState& s) override {
        char buf[LARGE_RECV_BUFFER]; // Use larger 64KB buffer
        for (;;) {
            int n = sock.receive(buf, sizeof(buf));
            if (n > 0) {
                s.request.append(buf, static_cast<size_t>(n));

                if (s.request.size() > MAX_REQUEST_BYTES) {
                    s.response = makeResponse("HTTP/1.1 413 Payload Too Large",
                        "text/plain; charset=utf-8", "Request too large.\n");
                    return ServerResult::KeepConnection;
                }

                if (s.response.empty() && requestComplete(s.request)) {
                    buildResponse(s);
                    return ServerResult::KeepConnection;
                }
            } else if (n == 0) {
                return ServerResult::Disconnect;
            } else {
                const auto err = sock.getLastError();
                if (err == SocketError::WouldBlock
                    || err == SocketError::Timeout) {
                    break; // no more data right now
                }
                return ServerResult::Disconnect;
            }
        }
        return ServerResult::KeepConnection;
    }

    ServerResult onWritable(TcpSocket& sock, HttpClientState& s) override {
        if (s.response.empty())
            return ServerResult::KeepConnection; // nothing to send yet

// Remove timing overhead for performance testing
#ifndef PERFORMANCE_TEST
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
            s.sent += static_cast<size_t>(sent);
        } else {
            const auto err = sock.getLastError();
            if (err == SocketError::WouldBlock || err == SocketError::Timeout) {
                return ServerResult::KeepConnection; // Try again later
            }
            return ServerResult::Disconnect; // Error
        }

        if (s.sent >= s.response.size()) {
#ifndef PERFORMANCE_TEST
            const auto elapsed
                = std::chrono::duration<double>(Clock::now() - s.startTime)
                      .count();
            const double mb
                = static_cast<double>(PAYLOAD_BYTES) / (1024.0 * 1024.0);
            std::cout << "Sent " << mb << " MB in " << elapsed << " s  ("
                      << (mb / elapsed) << " MB/s)\n";
#endif
            // Reset for next request - keep connection alive
            s.request.clear();
            s.response.clear();
            s.sent = 0;
            s.timerStarted = false;
            return ServerResult::KeepConnection; // Keep connection for pooling
        }
        return ServerResult::KeepConnection;
    }

    private:
    ServerResult onIdle() override {
        // Timing variables
        static auto last_call = Clock::now();
        static auto last_print = Clock::now();
        static std::vector<double> intervals;
        static int call_count = 0;
        static bool first_output_done = false;

        auto now = Clock::now();
        auto interval
            = std::chrono::duration<double, std::milli>(now - last_call)
                  .count();
        last_call = now;

        intervals.push_back(interval);
        call_count++;

        // Print first output after 500ms, then every 60 seconds
        auto since_last_print
            = std::chrono::duration<double>(now - last_print).count();
        auto print_interval = first_output_done ? 60.0 : 0.5;

        if (since_last_print >= print_interval) {
            if (!intervals.empty()) {
                double sum = 0;
                for (double i : intervals) {
                    sum += i;
                }
                double avg = sum / intervals.size();
                std::cout << "onIdle() called " << call_count
                          << " times, avg interval: " << avg << "ms ("
                          << (1000.0 / avg) << " Hz)\n";
            }

            // Reset for next period
            intervals.clear();
            call_count = 0;
            last_print = now;
            first_output_done = true;
        }

        return ServerResult::KeepConnection;
    }

    static void buildResponse(HttpClientState& s) {
        if (isHttpRequest(s.request)) {
            // Use static buffer for maximum performance
            s.response
                = makeResponse("HTTP/1.1 200 OK", "application/octet-stream",
                    std::string(preallocated_payload, PAYLOAD_BYTES));
        } else {
            s.response = makeResponse("HTTP/1.1 400 Bad Request",
                "text/plain; charset=utf-8",
                "Bad Request: this server only accepts HTTP requests.\n");
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
            .backlog = 64,
        });
        server.run(0, Milliseconds{1}); // 1ms timeout
        std::cout << "\nShutting down cleanly.\n";
    } catch (const SocketException& e) {
        std::cerr << "Server error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
