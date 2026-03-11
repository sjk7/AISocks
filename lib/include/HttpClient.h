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
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aiSocks {

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
    const std::map<std::string, std::string, std::less<>>& headers() const {
        return response.headers();
    }
};

// ---------------------------------------------------------------------------
// HttpClient -- High-level HTTP client (header-only implementation)
// ---------------------------------------------------------------------------
class HttpClient {
    public:
    struct Options {
        Milliseconds connectTimeout;
        Milliseconds requestTimeout;
        int maxRedirects;
        bool followRedirects;
        std::string userAgent;
        std::unordered_map<std::string, std::string> defaultHeaders;

        Options()
            : connectTimeout{30000}
            , requestTimeout{60000}
            , maxRedirects{10}
            , followRedirects{true}
            , userAgent{"AISocks-HttpClient/1.0"} {}

        Options& setHeader(std::string name, std::string value) {
            defaultHeaders[std::move(name)] = std::move(value);
            return *this;
        }

        /// Called just before opening a new TCP connection for a redirect.
        /// Arguments: fromUrl, toUrl, hop number (1-based).
        /// The old socket has already been closed; a new one is about to open.
        std::function<void(
            const std::string& from, const std::string& to, int hop)>
            onRedirect;
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
        std::unordered_map<std::string, std::string> headers;
        headers["Content-Type"] = contentType;
        return performRequest("POST", url, body, headers);
    }

    Result<HttpClientResponse> put(const std::string& url,
        const std::string& body = {},
        const std::string& contentType = "application/x-www-form-urlencoded") {
        std::unordered_map<std::string, std::string> headers;
        headers["Content-Type"] = contentType;
        return performRequest("PUT", url, body, headers);
    }

    Result<HttpClientResponse> del(const std::string& url) {
        return performRequest("DELETE", url, {}, {});
    }

    // Generic request method
    Result<HttpClientResponse> request(const std::string& method,
        const std::string& url, const std::string& body = {},
        const std::unordered_map<std::string, std::string>& headers = {}) {
        return performRequest(method, url, body, headers);
    }

    // Configuration
    void setOptions(Options options) { options_ = std::move(options); }
    const Options& getOptions() const noexcept { return options_; }

    private:
    Options options_;

    // Core request implementation
    Result<HttpClientResponse> performRequest(const std::string& method,
        const std::string& url, const std::string& body,
        const std::unordered_map<std::string, std::string>& headers) {
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

            // Build HTTP request using ClientHttpRequest
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

            while (true) {
                int n = socket.receive(buffer, sizeof(buffer));
                if (n < 0) {
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
                    // Create response wrapper
                    HttpClientResponse response{
                        parser.response(), currentUrl, redirectChain};

                    // Handle redirects
                    if (options_.followRedirects && response.isRedirect()) {
                        auto location = response.header("location");
                        if (!location) {
                            return Result<HttpClientResponse>::failure(
                                SocketError::Unknown,
                                "Redirect without Location header");
                        }

                        std::string locationStr(*location);

                        // Handle relative URLs
                        if (!locationStr.empty() && locationStr[0] == '/') {
                            std::string absoluteUrl = "http://" + hostPort;
                            int portValue = 80;
                            if (port != Port{80}) {
                                portValue = static_cast<int>(port.value());
                                absoluteUrl += ":" + std::to_string(portValue);
                            }
                            absoluteUrl += locationStr;
                            locationStr = std::move(absoluteUrl);
                        }

                        std::string fromUrl
                            = currentUrl; // URL whose response gave us this
                                          // redirect
                        currentUrl = locationStr;
                        redirectChain.push_back(currentUrl);
                        redirectDetected = true;
                        if (options_.onRedirect) {
                            // Inform the caller: old socket (fromUrl) is closed
                            // after this break; next for-loop iteration opens a
                            // fresh TCP connection to currentUrl.
                            options_.onRedirect(fromUrl, currentUrl,
                                static_cast<int>(redirectChain.size()));
                        }
                        break; // exit inner loop; outer for-loop makes the next
                               // request
                    }

                    return Result<HttpClientResponse>::success(
                        std::move(response));
                }
            }

            // Follow detected redirect in the next outer-loop iteration
            if (redirectDetected) continue;

            // Handle responses completed by feedEof() (Connection: close)
            if (parser.isComplete()) {
                return Result<HttpClientResponse>::success(HttpClientResponse{
                    parser.response(), currentUrl, redirectChain});
            }

            // If we get here, connection closed before response was complete
            return Result<HttpClientResponse>::failure(
                SocketError::ConnectionReset, "Incomplete response");
        }

        return Result<HttpClientResponse>::failure(
            SocketError::Unknown, "Too many redirects");
    }
};

} // namespace aiSocks
