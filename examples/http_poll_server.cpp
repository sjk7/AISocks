// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com



// Poll-driven HTTP/1.x server built on HttpPollServer.
// HttpPollServer handles all HTTP framing; this file only contains the
// application-level response logic.

#include "HttpPollServer.h"
#include "Socket.h"
#include "SocketTypes.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#define gmtime_r(timep, result) gmtime_s(result, timep)
#endif

using namespace aiSocks;

// Static response body served for GET / (612 bytes, no trailing newline).
static const char body[]
    = "<!DOCTYPE html>\n"
      "<html>\n"
      "<head>\n"
      "<title>Welcome to nginx!</title>\n"
      "<style>\n"
      "html { color-scheme: light dark; }\n"
      "body { width: 35em; margin: 0 auto;\n"
      "font-family: Tahoma, Verdana, Arial, sans-serif; }\n"
      "</style>\n"
      "</head>\n"
      "<body>\n"
      "<h1>Welcome to C++ http_server</h1>\n"
      "<p>If you see this page, the web server is successfully installed and\n"
      "working. Further configuration is required.</p>\n"
      "\n"
      "<p>For online documentation and support please refer to\n"
      "<a href=\"http://nginx.org/\">nginx.org</a>.<br/>\n"
      "Commercial support is available at\n"
      "<a href=\"http://nginx.com/\">nginx.com</a>.</p>\n"
      "\n"
      "<p><em>Thank you for using nginx.</em></p>\n"
      "</body>\n"
      "</html>";

// Header template: %s = RFC 7231 date, %zu = content-length,
// %s = "keep-alive" or "close"
static const char headerFmt[]
    = "HTTP/1.1 200 OK\r\n"
      "Server: nginx/1.29.5\r\n"
      "Date: %s\r\n"
      "Content-Type: text/html\r\n"
      "Content-Length: %zu\r\n"
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
static const char bigHeaderFmt[] = "HTTP/1.1 200 OK\r\n"
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
        snprintf(
            hdr, sizeof(hdr), headerFmt, date_buf, sizeof(body) - 1, "keep-alive");
        ka_response_ = std::string(hdr) + body;

        snprintf(hdr, sizeof(hdr), headerFmt, date_buf, sizeof(body) - 1, "close");
        close_response_ = std::string(hdr) + body;

        char bigHdr[256];
        snprintf(bigHdr, sizeof(bigHdr), bigHeaderFmt, date_buf, bigBody.size(),
            "keep-alive");
        big_ka_response_ = bigHdr + bigBody;

        snprintf(bigHdr, sizeof(bigHdr), bigHeaderFmt, date_buf, bigBody.size(),
            "close");
        
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

        const int port = static_cast<int>(bind.port.value());
        const bool isWildcard = (bind.address == "0.0.0.0"
            || bind.address == "::" || bind.address == "[::]" );

        if (isWildcard) {
            printf("Listening on <all> interfaces, port %d:\n", port);
            auto ifaces = Socket::getLocalAddresses();
            for (const auto& iface : ifaces) {
                // When bound to 0.0.0.0 show IPv4; when bound to :: show all.
                if (bind.address == "0.0.0.0"
                    && iface.family != AddressFamily::IPv4)
                    continue;
                printf("  %s%s\n", iface.address.c_str(),
                    iface.isLoopback ? "  (loopback)" : "");
            }
            printf("\n");
        } else {
            printf("Listening on %s:%d\n", bind.address.c_str(), port);
        }
    }

    protected:
    void buildResponse(HttpClientState& s) override {
        if (!isHttpRequest(s.request)) {
            s.responseView
                = cached_bad_request_; // zero-copy: view into static string
            return;
        }
        rebuildResponses();
        // Assign string_view directly into pre-built storage — no copy, no
        // allocation.
        if (s.request.find("GET /big ") != std::string::npos) {
            s.responseView
                = s.closeAfterSend ? big_close_response_ : big_ka_response_;
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
    
    // Print build info immediately after title
    HttpServer::printBuildInfo();

    HttpServer server(ServerBind{
        "0.0.0.0", Port{8080}, Backlog{Backlog::defaultBacklog}, true});
    if (!server.isValid()) {
        printf("Server failed to start\n");
        return 1;
    }

    server.run(ClientLimit::Unlimited, Milliseconds{0});
    return 0;
}
