// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, and Java:
// https://pvs-studio.com

#pragma once
// ---------------------------------------------------------------------------
// ClientHttpRequest.h -- HTTP/1.x request builder for client-side use
//
// Requires C++17 for string_view and structured bindings
//
// Builds HTTP requests from URLs and parameters. Designed for HttpClient
// to use instead of manually building request strings.
// ---------------------------------------------------------------------------

#include "FileIO.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aiSocks {

// ---------------------------------------------------------------------------
// ClientHttpRequest -- Builder for client-side HTTP requests
// ---------------------------------------------------------------------------
class ClientHttpRequest {
    public:
    /// Builder for HTTP requests
    class Builder {
        public:
        Builder() = default;

        Builder& method(std::string_view m) {
            method_ = m;
            return *this;
        }
        Builder& url(std::string_view u) {
            url_ = u;
            return *this;
        }
        Builder& body(std::string_view b) {
            body_ = b;
            return *this;
        }
        Builder& userAgent(std::string_view ua) {
            userAgent_ = ua;
            return *this;
        }
        Builder& accept(std::string_view a) {
            accept_ = a;
            return *this;
        }
        Builder& header(std::string_view name, std::string_view value);

        /// Build the complete HTTP request as a string
        std::string build() const;

        private:
        std::string_view method_{"GET"};
        std::string_view url_;
        std::string_view body_;
        std::string_view userAgent_{"AISocks-HttpClient/1.0"};
        std::string_view accept_{"*/*"};
        std::vector<std::pair<std::string_view, std::string_view>> headers_;

        // Parse URL components into string_views that reference the original
        // URL
        struct ParsedUrl {
            std::string_view scheme;
            std::string_view host;
            std::string_view port;
            std::string_view path;
        };

        static ParsedUrl parseUrl(std::string_view url);
    };

    static Builder builder() { return Builder(); }

    /// Build a GET request for the given URL with all sensible defaults.
    /// This is the simplest way to produce a ready-to-send HTTP/1.1 request.
    static std::string forUrl(std::string_view url) {
        return builder().url(url).build();
    }

    /// Build a POST request for the given URL with a body.
    static std::string forPost(std::string_view url, std::string_view body,
        std::string_view contentType = "application/x-www-form-urlencoded") {
        return builder()
            .method("POST")
            .url(url)
            .body(body)
            .header("Content-Type", contentType)
            .build();
    }
};

// ---------------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------------

inline ClientHttpRequest::Builder& ClientHttpRequest::Builder::header(
    std::string_view name, std::string_view value) {
    headers_.emplace_back(name, value);
    return *this;
}

inline std::string ClientHttpRequest::Builder::build() const {
    // Parse URL components efficiently using string_views
    ParsedUrl parsed = parseUrl(url_);

    // Build request using StringBuilder
    StringBuilder request(512);

    // Request line
    request.append(method_.data());
    request.append(" ");
    request.append(parsed.path.data(), parsed.path.size());
    request.append(" HTTP/1.1\r\n");

    // Host header
    request.append("Host: ");
    request.append(parsed.host.data(), parsed.host.size());
    if (!parsed.port.empty()) {
        request.append(":");
        request.append(parsed.port.data(), parsed.port.size());
    }
    request.append("\r\n");

    // User-Agent
    request.append("User-Agent: ");
    request.append(userAgent_.data(), userAgent_.size());
    request.append("\r\n");

    // Accept
    if (!accept_.empty()) {
        request.append("Accept: ");
        request.append(accept_.data(), accept_.size());
        request.append("\r\n");
    }

    // Connection
    request.append("Connection: close\r\n");

    // Custom headers
    for (const auto& header : headers_) {
        request.append(header.first.data(), header.first.size());
        request.append(": ");
        request.append(header.second.data(), header.second.size());
        request.append("\r\n");
    }

    // Content-Length for POST/PUT with body
    if (!body_.empty() && (method_ == "POST" || method_ == "PUT")) {
        request.append("Content-Length: ");
        request.append(std::to_string(body_.size()));
        request.append("\r\n");
    }

    // Empty line
    request.append("\r\n");

    // Body (if any)
    if (!body_.empty()) {
        request.append(body_.data(), body_.size());
    }

    return std::string(request.data());
}

inline ClientHttpRequest::Builder::ParsedUrl
ClientHttpRequest::Builder::parseUrl(std::string_view url) {
    ParsedUrl parsed;

    // Extract scheme
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        parsed.scheme = "";
        parsed.host = "";
        parsed.port = "";
        parsed.path = "/";
        return parsed;
    }

    parsed.scheme = url.substr(0, schemeEnd);

    // Extract host:port:path
    std::string_view remaining = url.substr(schemeEnd + 3);
    size_t pathStart = remaining.find('/');
    if (pathStart == std::string::npos) pathStart = remaining.size();

    std::string_view hostPort = remaining.substr(0, pathStart);
    parsed.path = (pathStart < remaining.size()) ? remaining.substr(pathStart)
                                                 : std::string_view("/");

    // Extract port from host
    size_t portPos = hostPort.find(':');
    if (portPos == std::string::npos) {
        parsed.host = hostPort;
        parsed.port = "";
    } else {
        parsed.host = hostPort.substr(0, portPos);
        parsed.port = hostPort.substr(portPos + 1);
    }

    return parsed;
}

} // namespace aiSocks
