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
    /// If location is already absolute (contains "://") it is returned as-is.
    /// If it starts with '/' it is resolved against the scheme+host of base.
    static std::string resolveUrl(
        const std::string& base, const std::string& location) {
        if (location.find("://") != std::string::npos) return location;
        if (!location.empty() && location[0] == '/') {
            size_t schemeEnd = base.find("://");
            size_t hostEnd = (schemeEnd != std::string::npos)
                ? base.find('/', schemeEnd + 3)
                : std::string::npos;
            std::string origin = (hostEnd != std::string::npos)
                ? base.substr(0, hostEnd)
                : base;
            return origin + location;
        }
        return location;
    }

    private:
    Options options_;

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

            std::string hostPort = remaining.substr(0, pathStart);

            // Extract port from host if specified
            Port port{80};
            size_t portPos = hostPort.find(':');
            if (portPos != std::string::npos) {
                std::string portStr = hostPort.substr(portPos + 1);
                hostPort = hostPort.substr(0, portPos);
                try {
                    int portNum = std::stoi(portStr);
                    if (portNum >= 1 && portNum <= 65535) {
                        port = Port{static_cast<uint16_t>(portNum)};
                    }
                } catch (...) {
                    port = Port{80};
                }
            }

            // Connect using existing SocketFactory
            ConnectArgs args{hostPort, port, options_.connectTimeout};
            auto socketResult = SocketFactory::createTcpClient(args);
            if (!socketResult.isSuccess()) {
                return Result<HttpClientResponse>::failure(
                    SocketError::ConnectFailed, "Connection failed");
            }

            TcpSocket socket = std::move(socketResult.value());

            // Apply the per-request receive timeout so that a server that
            // accepts the connection but never sends a response does not
            // block the caller indefinitely.
            if (options_.requestTimeout.count > 0)
                socket.setReceiveTimeout(options_.requestTimeout);

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
