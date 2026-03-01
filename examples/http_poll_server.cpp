// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Poll-driven HTTP/1.x server built on HttpPollServer.
// HttpPollServer handles all HTTP framing; this file only contains the
// application-level response logic.

#include "HttpPollServer.h"
#include "SocketTypes.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

using namespace aiSocks;

// Response body matching the reference server exactly (251 bytes, no trailing
// newline).
static const char body[]
    = "<!DOCTYPE html>\n"
      "<html lang=\"en\">\n"
      "\n"
      "<head>\n"
      "    <meta charset=\"UTF-8\">\n"
      "    <meta name=\"viewport\" content=\"width=device-width, "
      "initial-scale=1.0\">\n"
      "    <title>C++ App</title>\n"
      "    <h1>\n"
      "        Welcome to my world!\n"
      "    </h1>\n"
      "</head>\n"
      "\n"
      "<body>\n"
      "\n"
      "</body>\n"
      "\n"
      "</html>";

// Header template: %s = RFC 7231 date, %s = "keep-alive" or "close"
static const char headerFmt[]
    = "HTTP/1.1 200 OK\r\n"
      "Server: nginx/1.29.5\r\n"
      "Date: %s\r\n"
      "Content-Type: text/html\r\n"
      "Content-Length: 251\r\n"
      "Last-Modified: Fri, 11 Oct 2024 01:06:56 GMT\r\n"
      "Connection: %s\r\n"
      "ETag: \"67087a30-fb\"\r\n"
      "Accept-Ranges: bytes\r\n"
      "\r\n";

// 1 MB payload for /big — filled once at startup with repeating pattern.
static std::string makeBigBody() {
    static constexpr size_t bigSize = 1024 * 1024;
    std::string s(bigSize, '\0');
    // Fill with a repeating ASCII pattern so it compresses poorly (realistic).
    for (size_t i = 0; i < bigSize; ++i)
        s[i] = static_cast<char>('A' + (i % 26));
    return s;
}
static const std::string bigBody = makeBigBody();

// Header template for /big: %s = date, %s = connection, %zu = content-length
static const char bigHeaderFmt[]
    = "HTTP/1.1 200 OK\r\n"
      "Server: nginx/1.29.5\r\n"
      "Date: %s\r\n"
      "Content-Type: application/octet-stream\r\n"
      "Content-Length: %zu\r\n"
      "Connection: %s\r\n"
      "\r\n";

class HttpServer : public HttpPollServer {
    private:
    // Full pre-built responses, rebuilt once per second when the Date changes.
    std::string ka_response_;
    std::string close_response_;
    std::string big_ka_response_;
    std::string big_close_response_;
    std::string cached_bad_request_;
    time_t last_time_ = 0;

    void rebuildResponses() {
        time_t now = time(nullptr);
        if (now == last_time_) return;
        last_time_ = now;

        char date_buf[64];
        struct tm tm_buf;
        gmtime_r(&now, &tm_buf);
        strftime(
            date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_buf);

        char hdr[512];
        snprintf(hdr, sizeof(hdr), headerFmt, date_buf, "keep-alive");
        ka_response_ = std::string(hdr) + body;

        snprintf(hdr, sizeof(hdr), headerFmt, date_buf, "close");
        close_response_ = std::string(hdr) + body;

        char bigHdr[256];
        snprintf(bigHdr, sizeof(bigHdr), bigHeaderFmt, date_buf, bigBody.size(), "keep-alive");
        big_ka_response_ = bigHdr + bigBody;

        snprintf(bigHdr, sizeof(bigHdr), bigHeaderFmt, date_buf, bigBody.size(), "close");
        big_close_response_ = bigHdr + bigBody;
    }

    public:
    explicit HttpServer(const ServerBind& bind) : HttpPollServer(bind) {
        cached_bad_request_
            = "HTTP/1.1 400 Bad Request\r\n"
              "Content-Type: text/plain; charset=utf-8\r\n"
              "Content-Length: 52\r\n"
              "\r\n"
              "Bad Request: this server only accepts HTTP requests.\n";

        rebuildResponses(); // warm the cache before the first request
        setKeepAliveTimeout(std::chrono::milliseconds{5000});
        printf("Listening on %s:%d\n", bind.address.c_str(),
            static_cast<int>(bind.port));
    }

    protected:
    void buildResponse(HttpClientState& s) override {
        if (!isHttpRequest(s.request)) {
            s.responseView = cached_bad_request_; // zero-copy: view into static string
            return;
        }
        rebuildResponses();
        // Assign string_view directly into pre-built storage — no copy, no allocation.
        if (s.request.find("GET /big ") != std::string::npos) {
            s.responseView = s.closeAfterSend ? big_close_response_ : big_ka_response_;
        } else {
            s.responseView = s.closeAfterSend ? close_response_ : ka_response_;
        }
    }
};

// for more backlog in Mac:
/*/
    sudo pico /etc/sysctl.conf
    kern.maxfiles=65536
    kern.maxfilesperproc=65536
    kern.ipc.somaxconn=4096
/*/

/*/
    *nix stress test:
    ulimit -n 65536; wrk -t12 -c15000 -d30s -H "Connection: keep-alive"
http://localhost:8080/ 2>&1

/*/

int main() {
    printf("=== Poll-Driven HTTP Server ===\n");

    HttpServer server(ServerBind{
        "0.0.0.0", Port{8080}, Backlog{Backlog::defaultBacklog}, true});
    if (!server.isValid()) {
        printf("Server failed to start\n");
        return 1;
    }

    server.run(ClientLimit::Unlimited, Milliseconds{0});
    printf("\nShutting down cleanly.\n");
    return 0;
}
