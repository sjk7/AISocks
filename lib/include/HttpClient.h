// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, and Java:
// https://pvs-studio.com

#pragma once
// ---------------------------------------------------------------------------
// HttpClient.h -- High-level HTTP/1.x client with redirect following
//
// Requires C++17 for string_view and structured bindings
//
// Uses existing HttpResponse.h and HttpResponseParser for response parsing.
// Uses existing ConnectArgs and SocketFactory for connections.
// Uses ClientHttpRequest for building HTTP requests.
// ---------------------------------------------------------------------------

#include "HttpResponse.h"
#include "ClientHttpRequest.h"
#include "SocketFactory.h"
#include "SocketTypes.h"
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace aiSocks {

// ---------------------------------------------------------------------------
// IRedirectListener -- optional callback interface for redirect notifications
//
// Implement this interface and assign a pointer to Options::redirectListener
// to be notified before each new TCP connection is opened for a redirect.
//
// The listener pointer is non-owning and must outlive the HttpClient.
// Leave Options::redirectListener as nullptr (the default) to opt out.
//
// Example:
//   struct MyListener : aiSocks::IRedirectListener {
//       void onRedirect(const std::string& from,
//                       const std::string& to, int hop) noexcept override {
//           printf("hop %d: %s -> %s\n", hop, from.c_str(), to.c_str());
//       }
//   } listener;
//   aiSocks::HttpClient::Options opts;
//   opts.redirectListener = &listener;
// ---------------------------------------------------------------------------
struct IRedirectListener {
    virtual void onRedirect(
        const std::string& from, const std::string& to, int hop) noexcept
        = 0;
    virtual ~IRedirectListener() = default;
};

// ---------------------------------------------------------------------------
// HttpClientResponse -- Wrapper around HttpResponse with redirect metadata
// ---------------------------------------------------------------------------
struct HttpClientResponse {
    HttpResponse response; // Owned parsed response
    std::string finalUrl; // After following redirects
    std::vector<std::string> redirectChain; // URLs visited during redirects

    // Forward common accessors to underlying HttpResponse
    int statusCode() const { return response.statusCode; }
    std::string_view version() const { return response.version(); }
    std::string_view statusText() const { return response.statusText(); }
    std::string_view body() const { return response.body(); }

    // Header access
    const std::string* header(std::string_view name) const {
        return response.header(name);
    }
    std::string_view headerOr(
        std::string_view name, std::string_view fallback = {}) const {
        const auto* h = header(name);
        return h ? std::string_view(*h) : fallback;
    }

    // Convenience accessors
    std::string_view contentType() const { return headerOr("content-type"); }
    std::string_view contentLength() const {
        return headerOr("content-length");
    }
    bool isSuccess() const { return statusCode() >= 200 && statusCode() < 300; }
    bool isRedirect() const {
        return statusCode() == 301 || statusCode() == 302 || statusCode() == 307
            || statusCode() == 308;
    }

    // Direct access to headers map
    const HeaderMap& headers() const { return response.headers(); }
};

// ---------------------------------------------------------------------------
// HttpClient -- High-level HTTP client (header-only implementation)
// ---------------------------------------------------------------------------
class HttpClient {
    public:
    struct Options {
        /// TCP connect timeout in milliseconds.
        /// 0 = no timeout (block until connected or OS-level error).
        /// Default: 30 000 ms (30 s).
        Milliseconds connectTimeout;

        /// Time allowed to receive the complete response, in milliseconds.
        /// 0 = no timeout.  Default: 60 000 ms (60 s).
        Milliseconds requestTimeout;

        /// Maximum number of redirects to follow before giving up.
        /// Must be >= 0.  0 = do not follow any redirects (same as
        /// followRedirects = false).  Default: 10.
        int maxRedirects;

        /// When true (default), 3xx responses are followed automatically and
        /// the final non-redirect response is returned to the caller.
        /// Set to false to receive 3xx responses directly (e.g. for manual
        /// redirect handling or diagnostic purposes).
        bool followRedirects;

        /// Value sent in the User-Agent request header.
        /// Empty string omits the header entirely.
        /// Default: "AISocks-HttpClient/1.0".
        std::string userAgent;

        /// Headers added to every request made by this client instance.
        /// Per-request headers (passed to post(), request(), etc.) take
        /// precedence over these defaults when names collide.
        HeaderMap defaultHeaders;

        Options()
            : connectTimeout{30000}
            , requestTimeout{60000}
            , maxRedirects{10}
            , followRedirects{true}
            , userAgent{"AISocks-HttpClient/1.0"} {}

        /// Convenience fluent setter for defaultHeaders.
        Options& setHeader(std::string name, std::string value) {
            defaultHeaders[std::move(name)] = std::move(value);
            return *this;
        }

        /// Optional redirect notification interface.
        /// Points to a caller-owned IRedirectListener; must outlive this
        /// HttpClient.  Called after the old socket closes and before the new
        /// TCP connection opens.  nullptr (default) = no notifications.
        IRedirectListener* redirectListener{nullptr};
    };

    explicit HttpClient(Options options = Options{})
        : options_(std::move(options)) {}

    // HTTP methods
    Result<HttpClientResponse> get(const std::string& url) {
        return performRequest("GET", url, {}, {});
    }

    Result<HttpClientResponse> post(const std::string& url,
        const std::string& body = {},
        const std::string& contentType = "application/x-www-form-urlencoded") {
        HeaderMap headers;
        headers["Content-Type"] = contentType;
        return performRequest("POST", url, body, headers);
    }

    Result<HttpClientResponse> put(const std::string& url,
        const std::string& body = {},
        const std::string& contentType = "application/x-www-form-urlencoded") {
        HeaderMap headers;
        headers["Content-Type"] = contentType;
        return performRequest("PUT", url, body, headers);
    }

    Result<HttpClientResponse> del(const std::string& url) {
        return performRequest("DELETE", url, {}, {});
    }

    // Generic request method
    Result<HttpClientResponse> request(const std::string& method,
        const std::string& url, const std::string& body = {},
        const HeaderMap& headers = {}) {
        return performRequest(method, url, body, headers);
    }

    // Configuration
    void setOptions(Options options) { options_ = std::move(options); }
    const Options& getOptions() const noexcept { return options_; }

    /// Resolve a Location header value against the base URL that produced it.
    /// Supports absolute, scheme-relative, root-relative, query-relative,
    /// and path-relative references with dot-segment normalization.
    static std::string resolveUrl(
        const std::string& base, const std::string& location) {
        if (location.empty()) return base;

        if (location.find("://") != std::string::npos) return location;

        const size_t schemeEnd = base.find("://");
        if (schemeEnd == std::string::npos) return location;

        const std::string scheme = base.substr(0, schemeEnd);
        const size_t authStart = schemeEnd + 3;
        const size_t pathStart = base.find('/', authStart);
        const std::string origin = (pathStart == std::string::npos)
            ? base
            : base.substr(0, pathStart);

        if (location.size() >= 2 && location[0] == '/' && location[1] == '/')
            return scheme + ":" + location;

        if (location[0] == '/') return origin + location;

        const size_t queryPos = base.find('?', authStart);
        const size_t fragPos = base.find('#', authStart);
        size_t basePathEnd = std::string::npos;
        if (queryPos != std::string::npos && fragPos != std::string::npos)
            basePathEnd = (queryPos < fragPos) ? queryPos : fragPos;
        else if (queryPos != std::string::npos)
            basePathEnd = queryPos;
        else
            basePathEnd = fragPos;

        const std::string basePath = (pathStart == std::string::npos)
            ? "/"
            : base.substr(pathStart,
                  (basePathEnd == std::string::npos)
                      ? std::string::npos
                      : (basePathEnd - pathStart));

        if (location[0] == '?') return origin + basePath + location;
        if (location[0] == '#') {
            std::string out = base;
            const size_t oldFrag = out.find('#', authStart);
            if (oldFrag != std::string::npos) out.resize(oldFrag);
            out += location;
            return out;
        }

        size_t cut = location.find('?');
        const size_t hashCut = location.find('#');
        if (cut == std::string::npos
            || (hashCut != std::string::npos && hashCut < cut))
            cut = hashCut;
        const std::string relPath = location.substr(0, cut);
        const std::string suffix
            = (cut == std::string::npos) ? std::string{} : location.substr(cut);

        std::string baseDir = basePath;
        const size_t lastSlash = baseDir.rfind('/');
        baseDir = (lastSlash == std::string::npos)
            ? "/"
            : baseDir.substr(0, lastSlash + 1);

        std::string merged = baseDir + relPath;
        std::vector<std::string> segs;
        segs.reserve(8);
        size_t i = 0;
        while (i <= merged.size()) {
            const size_t j = merged.find('/', i);
            const std::string seg = (j == std::string::npos)
                ? merged.substr(i)
                : merged.substr(i, j - i);

            if (seg.empty() || seg == ".") {
                // ignore
            } else if (seg == "..") {
                if (!segs.empty()) segs.pop_back();
            } else {
                segs.push_back(seg);
            }

            if (j == std::string::npos) break;
            i = j + 1;
        }

        std::string normalized = "/";
        for (size_t k = 0; k < segs.size(); ++k) {
            if (k) normalized.push_back('/');
            normalized += segs[k];
        }

        return origin + normalized + suffix;
    }

    private:
    Options options_;

    static bool parseAuthority_(
        const std::string& authority, std::string& hostOut, Port& portOut) {
        hostOut.clear();
        portOut = Port{80};
        if (authority.empty()) return false;

        if (authority.front() == '[') {
            const size_t rb = authority.find(']');
            if (rb == std::string::npos) return false;

            hostOut = authority.substr(1, rb - 1); // connect() wants raw IPv6
            if (rb + 1 < authority.size()) {
                if (authority[rb + 1] != ':') return false;
                const std::string portStr = authority.substr(rb + 2);
                if (!portStr.empty()) {
                    try {
                        const int portNum = std::stoi(portStr);
                        if (portNum < 1 || portNum > 65535) return false;
                        portOut = Port{static_cast<uint16_t>(portNum)};
                    } catch (...) {
                        return false;
                    }
                }
            }
            return true;
        }

        const size_t colon = authority.find(':');
        if (colon != std::string::npos
            && authority.find(':', colon + 1) == std::string::npos) {
            hostOut = authority.substr(0, colon);
            const std::string portStr = authority.substr(colon + 1);
            if (!portStr.empty()) {
                try {
                    const int portNum = std::stoi(portStr);
                    if (portNum < 1 || portNum > 65535) return false;
                    portOut = Port{static_cast<uint16_t>(portNum)};
                } catch (...) {
                    return false;
                }
            }
            return !hostOut.empty();
        }

        hostOut = authority;
        return true;
    }

    // Core request implementation
    Result<HttpClientResponse> performRequest(const std::string& method,
        const std::string& url, const std::string& body,
        const HeaderMap& headers) {
        std::vector<std::string> redirectChain;
        std::string currentUrl = url;

        for (int redirectCount = 0; redirectCount <= options_.maxRedirects;
            ++redirectCount) {
            // Extract host and port from URL for ConnectArgs
            size_t schemeEnd = currentUrl.find("://");
            if (schemeEnd == std::string::npos) {
                return Result<HttpClientResponse>::failure(
                    SocketError::Unknown, "Invalid URL format");
            }

            std::string remaining = currentUrl.substr(schemeEnd + 3);
            size_t pathStart = remaining.find('/');
            if (pathStart == std::string::npos) pathStart = remaining.size();

            const std::string authority = remaining.substr(0, pathStart);
            std::string host;
            Port port{80};
            if (!parseAuthority_(authority, host, port)) {
                return Result<HttpClientResponse>::failure(
                    SocketError::Unknown, "Invalid URL authority");
            }

            // Connect using existing SocketFactory
            ConnectArgs args{host, port, options_.connectTimeout};
            auto socketResult = SocketFactory::createTcpClient(args);
            if (!socketResult.isSuccess()) {
                return Result<HttpClientResponse>::failure(
                    SocketError::ConnectFailed, "Connection failed");
            }

            TcpSocket socket = std::move(socketResult.value());

            const bool boundedRequest = options_.requestTimeout.count > 0;
            const auto requestDeadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(options_.requestTimeout.count);

            auto requestBuilder = ClientHttpRequest::builder()
                                      .method(method)
                                      .url(currentUrl)
                                      .userAgent(options_.userAgent);

            // Add headers
            for (const auto& header : options_.defaultHeaders) {
                requestBuilder.header(header.first, header.second);
            }
            for (const auto& header : headers) {
                requestBuilder.header(header.first, header.second);
            }

            // Add body for POST/PUT
            if (!body.empty()) {
                requestBuilder.body(body);
            }

            std::string request = requestBuilder.build();
            if (!socket.sendAll(request.data(), request.size())) {
                return Result<HttpClientResponse>::failure(
                    SocketError::SendFailed, "Failed to send request");
            }

            // Parse response using existing HttpResponseParser
            HttpResponseParser parser;
            char buffer[8192];
            bool redirectDetected = false;

            // Shared handler for a fully-parsed response: sets finalResult
            // when done, or sets redirectDetected and returns without a result
            // when a redirect should be followed.
            Result<HttpClientResponse> finalResult
                = Result<HttpClientResponse>::failure(SocketError::Unknown, "");
            bool haveResult = false;
            auto handleComplete = [&]() {
                HttpClientResponse resp{
                    parser.response(), currentUrl, redirectChain};
                if (options_.followRedirects && resp.isRedirect()) {
                    auto location = resp.header("location");
                    if (!location) {
                        finalResult = Result<HttpClientResponse>::failure(
                            SocketError::Unknown,
                            "Redirect without Location header");
                        haveResult = true;
                        return;
                    }
                    std::string fromUrl = currentUrl;
                    currentUrl = resolveUrl(currentUrl, *location);
                    redirectChain.push_back(currentUrl);
                    redirectDetected = true;
                    if (options_.redirectListener)
                        options_.redirectListener->onRedirect(fromUrl,
                            currentUrl, static_cast<int>(redirectChain.size()));
                    return; // follow redirect in the outer loop
                }
                finalResult
                    = Result<HttpClientResponse>::success(std::move(resp));
                haveResult = true;
            };

            while (true) {
                if (boundedRequest) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto remainingMs
                        = std::chrono::duration_cast<std::chrono::milliseconds>(
                            requestDeadline - now)
                              .count();
                    if (remainingMs <= 0) {
                        return Result<HttpClientResponse>::failure(
                            SocketError::Timeout, "Request timed out");
                    }
                    socket.setReceiveTimeout(Milliseconds{remainingMs});
                }

                int n = socket.receive(buffer, sizeof(buffer));
                if (n < 0) {
                    const auto err = socket.getLastError();
                    if (err == SocketError::Timeout
                        || err == SocketError::WouldBlock)
                        return Result<HttpClientResponse>::failure(
                            SocketError::Timeout, "Request timed out");
                    return Result<HttpClientResponse>::failure(
                        SocketError::ReceiveFailed, "Receive error");
                }
                if (n == 0) {
                    parser
                        .feedEof(); // signal EOF for connection-close responses
                    break;
                }

                auto state = parser.feed(buffer, n);

                if (state == HttpResponseParser::State::Error) {
                    return Result<HttpClientResponse>::failure(
                        SocketError::Unknown, "Response parse error");
                }

                if (state == HttpResponseParser::State::Complete) {
                    handleComplete();
                    if (haveResult) return finalResult;
                    break; // redirect detected; outer loop opens next
                           // connection
                }
            }

            // Follow detected redirect in the next outer-loop iteration.
            if (redirectDetected) continue;

            // Handle responses completed by feedEof() (Connection: close),
            // including the (rare) case of a connection-close redirect.
            if (parser.isComplete()) {
                handleComplete();
                if (haveResult) return finalResult;
                if (redirectDetected) continue;
            }

            // Connection closed before response was complete.
            if (!redirectDetected) {
                return Result<HttpClientResponse>::failure(
                    SocketError::ConnectionReset, "Incomplete response");
            }
        }

        return Result<HttpClientResponse>::failure(
            SocketError::Unknown, "Too many redirects");
    }
};

} // namespace aiSocks
