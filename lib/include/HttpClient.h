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
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifdef AISOCKS_ENABLE_TLS
#include "TlsOpenSsl.h"
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

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
        /// Must be > 0.
        ///
        /// NOTE: aiSocks reserves Milliseconds{0} for poller-driven async
        /// connect-in-progress semantics (WouldBlock) in ConnectArgs.
        /// HttpClient is synchronous, so it rejects 0 to avoid accidental
        /// non-blocking connect behavior.
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

#ifdef AISOCKS_ENABLE_TLS
        /// Verify the server TLS certificate chain against system CA paths.
        /// Default: true.
        bool verifyCertificate{true};

        /// Optional PEM trust bundle/file used when verifyCertificate is true.
        /// Empty means system default CA paths.
        std::string caCertFile;

        /// Optional CA certificate directory used when verifyCertificate is
        /// true. On OpenSSL, this should contain hash-named cert symlinks or
        /// files created via c_rehash/openssl rehash.
        /// Empty means no explicit CA directory.
        std::string caCertDir;

        /// Maximum certificate-chain verification depth when
        /// verifyCertificate is true.
        /// -1 keeps OpenSSL defaults.
        int verifyDepth{-1};
#endif
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
    void setOptions(Options options) {
        options_ = std::move(options);
        clearCachedConnection_();
#ifdef AISOCKS_ENABLE_TLS
        cachedClientTlsContext_.reset();
#endif
    }
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
        const bool keepTrailingSlash
            = !relPath.empty() && relPath.back() == '/';

        std::string baseDir = basePath;
        const size_t lastSlash = baseDir.rfind('/');
        baseDir = (lastSlash == std::string::npos)
            ? "/"
            : baseDir.substr(0, lastSlash + 1);

        std::string merged;
        merged.reserve(baseDir.size() + relPath.size());
        merged += baseDir;
        merged += relPath;
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

        std::string normalized;
        normalized.reserve(merged.size() + 1);
        normalized.push_back('/');
        for (size_t k = 0; k < segs.size(); ++k) {
            if (k) normalized.push_back('/');
            normalized += segs[k];
        }
        if (keepTrailingSlash && normalized.back() != '/') {
            normalized.push_back('/');
        }

        return origin + normalized + suffix;
    }

    private:
    Options options_;
    std::string cachedHost_;
    Port cachedPort_{80};
    std::shared_ptr<TcpSocket> cachedSocket_;
#ifdef AISOCKS_ENABLE_TLS
    std::shared_ptr<TlsSession> cachedTlsSession_;
    std::shared_ptr<TlsContext> cachedClientTlsContext_;
#endif

    void clearCachedConnection_() noexcept {
        cachedSocket_.reset();
#ifdef AISOCKS_ENABLE_TLS
        cachedTlsSession_.reset();
#endif
    }

    static bool hasTokenCI_(std::string_view field, std::string_view token) {
        auto trim = [](std::string_view s) {
            size_t begin = 0;
            size_t end = s.size();
            while (begin < end
                && std::isspace(static_cast<unsigned char>(s[begin])))
                ++begin;
            while (end > begin
                && std::isspace(static_cast<unsigned char>(s[end - 1])))
                --end;
            return s.substr(begin, end - begin);
        };

        auto equalsCI = [](std::string_view a, std::string_view b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                const char ac = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(a[i])));
                const char bc = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(b[i])));
                if (ac != bc) return false;
            }
            return true;
        };

        token = trim(token);
        if (token.empty()) return false;

        size_t pos = 0;
        while (pos <= field.size()) {
            const size_t comma = field.find(',', pos);
            const std::string_view part = (comma == std::string::npos)
                ? field.substr(pos)
                : field.substr(pos, comma - pos);
            if (equalsCI(trim(part), token)) return true;
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        return false;
    }

    static bool shouldKeepAlive_(const HttpResponse& resp) {
        const auto conn = resp.header("connection");
        const std::string_view connValue
            = conn ? std::string_view(*conn) : std::string_view{};
        const std::string_view version = resp.version();

        if (version == "HTTP/1.0") {
            return hasTokenCI_(connValue, "keep-alive");
        }

        return !hasTokenCI_(connValue, "close");
    }

    static bool hasHeaderCI_(const HeaderMap& headers, std::string_view name) {
        auto equalsCI = [](std::string_view a, std::string_view b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                const char ac = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(a[i])));
                const char bc = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(b[i])));
                if (ac != bc) return false;
            }
            return true;
        };

        for (const auto& header : headers) {
            if (equalsCI(header.first, name)) return true;
        }
        return false;
    }

#ifdef AISOCKS_ENABLE_TLS
    static std::string normalizeTlsHost_(std::string host) {
        while (host.size() > 1 && host.back() == '.') {
            host.pop_back();
        }
        return host;
    }

    static bool isLikelyIpLiteral_(const std::string& host) {
        return Socket::isValidIPv4(host) || Socket::isValidIPv6(host);
    }

    static bool hasNonAsciiHostChar_(const std::string& host) {
        for (unsigned char c : host) {
            if (c > 0x7F) return true;
        }
        return false;
    }
#endif

    static bool headerHasTokenCI_(const HeaderMap& headers,
        std::string_view name, std::string_view token) {
        auto equalsCI = [](std::string_view a, std::string_view b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                const char ac = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(a[i])));
                const char bc = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(b[i])));
                if (ac != bc) return false;
            }
            return true;
        };

        for (const auto& header : headers) {
            if (equalsCI(header.first, name)
                && hasTokenCI_(header.second, token)) {
                return true;
            }
        }
        return false;
    }

    static bool parseAuthority_(const std::string& authority,
        std::string& hostOut, Port& portOut,
        Port defaultPort = Port{uint16_t{80}}) {
        hostOut.clear();
        portOut = defaultPort;
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
        if (options_.connectTimeout.count <= 0) {
            return Result<HttpClientResponse>::failure(SocketError::Unknown,
                "HttpClient Options.connectTimeout must be > 0 ms");
        }

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

            std::string scheme = currentUrl.substr(0, schemeEnd);
            for (char& c : scheme)
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            const bool isHttps = (scheme == "https");
            if (!isHttps && scheme != "http") {
                return Result<HttpClientResponse>::failureOwned(
                    SocketError::Unknown, "Unsupported URL scheme: " + scheme);
            }
#ifndef AISOCKS_ENABLE_TLS
            if (isHttps) {
                return Result<HttpClientResponse>::failure(
                    SocketError::Unknown, "HTTPS is not supported");
            }
#endif

            std::string remaining = currentUrl.substr(schemeEnd + 3);
            size_t pathStart = remaining.find('/');
            if (pathStart == std::string::npos) pathStart = remaining.size();

            const std::string authority = remaining.substr(0, pathStart);
            std::string host;
            const Port defaultPort{isHttps ? uint16_t{443} : uint16_t{80}};
            Port port{defaultPort};
            if (!parseAuthority_(authority, host, port, defaultPort)) {
                return Result<HttpClientResponse>::failure(
                    SocketError::Unknown, "Invalid URL authority");
            }
#ifdef AISOCKS_ENABLE_TLS
            if (isHttps && options_.verifyCertificate
                && options_.verifyDepth < -1) {
                return Result<HttpClientResponse>::failure(
                    SocketError::Unknown, "TLS verifyDepth must be -1 or >= 0");
            }
            const std::string normalizedVerifyHost = normalizeTlsHost_(host);
            const bool normalizedVerifyHostIsIp
                = isLikelyIpLiteral_(normalizedVerifyHost);
            if (isHttps && options_.verifyCertificate
                && !normalizedVerifyHostIsIp
                && hasNonAsciiHostChar_(normalizedVerifyHost)) {
                return Result<HttpClientResponse>::failure(SocketError::Unknown,
                    "TLS DNS host contains non-ASCII characters; use "
                    "punycode (A-label)");
            }
#endif

            std::shared_ptr<TcpSocket> socket;
            bool reusedConnection = false;
            bool retriedReusedConnection = false;
            const bool connectAsIpv6 = Socket::isValidIPv6(host);
            if (cachedSocket_ && cachedSocket_->isValid() && cachedHost_ == host
                && cachedPort_.value() == port.value()) {
                socket = cachedSocket_;
                reusedConnection = true;
            } else {
                ConnectArgs args{host, port, options_.connectTimeout};
                auto socketResult = connectAsIpv6
                    ? SocketFactory::createTcpClient(AddressFamily::IPv6, args)
                    : SocketFactory::createTcpClient(args);
                if (!socketResult.isSuccess()) {
                    return Result<HttpClientResponse>::failureOwned(
                        socketResult.error(),
                        "Connection failed to " + host + ":"
                            + std::to_string(port.value()) + " - "
                            + socketResult.message());
                }
                socket = std::make_shared<TcpSocket>(
                    std::move(socketResult.value()));
            }

#ifdef AISOCKS_ENABLE_TLS
            // Per-iteration TLS state and helpers.
            std::shared_ptr<TlsSession> tlsSession;
            std::string tlsSetupError;
            // Creates a fresh TLS client session for the current socket and
            // performs the blocking handshake.  Captures socket by reference
            // so it can be reused after an inline retry reconnect.
            auto setupTlsForCurrentSocket = [&]() -> bool {
                if (!isHttps) return true;
                tlsSetupError.clear();
                tlsSession.reset();
                if (!cachedClientTlsContext_) {
                    auto ctx = TlsContext::create(
                        TlsContext::Mode::Client, &tlsSetupError);
                    if (!ctx) return false;
                    if (!ctx->configureVerifyPeer(options_.verifyCertificate,
                            options_.verifyCertificate, options_.caCertFile,
                            options_.caCertDir, &tlsSetupError)) {
                        return false;
                    }
                    cachedClientTlsContext_
                        = std::shared_ptr<TlsContext>(std::move(ctx));
                }
                auto sess = TlsSession::create(
                    cachedClientTlsContext_->nativeHandle(), &tlsSetupError);
                if (!sess) return false;
                if (!sess->attachSocket(
                        static_cast<int>(socket->getNativeHandle()),
                        &tlsSetupError))
                    return false;
                if (options_.verifyCertificate) {
                    X509_VERIFY_PARAM* verifyParam
                        = SSL_get0_param(sess->nativeHandle());
                    if (!verifyParam) {
                        tlsSetupError = "TLS verify parameter setup failed";
                        return false;
                    }
                    if (options_.verifyDepth >= 0) {
                        X509_VERIFY_PARAM_set_depth(
                            verifyParam, options_.verifyDepth);
                    }

                    const int hostSet = normalizedVerifyHostIsIp
                        ? X509_VERIFY_PARAM_set1_ip_asc(
                              verifyParam, normalizedVerifyHost.c_str())
                        : X509_VERIFY_PARAM_set1_host(
                              verifyParam, normalizedVerifyHost.c_str(), 0);
                    if (hostSet != 1) {
                        tlsSetupError = "TLS hostname verification setup "
                                        "failed for host: "
                            + normalizedVerifyHost;
                        return false;
                    }
                }
                // Set SNI hostname so virtual-hosted servers pick the right
                // cert.
                if (!normalizedVerifyHost.empty() && !normalizedVerifyHostIsIp)
                    SSL_set_tlsext_host_name(
                        sess->nativeHandle(), normalizedVerifyHost.c_str());
                sess->setConnectState();
                for (;;) {
                    const int r = sess->handshake();
                    if (r == 1) break;
                    const int e = sess->getLastErrorCode(r);
                    if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
                        continue;
                    tlsSetupError = "TLS handshake failed: "
                        + TlsOpenSsl::lastErrorString();
                    return false;
                }
                if (options_.verifyCertificate) {
                    X509* peerCert
                        = SSL_get_peer_certificate(sess->nativeHandle());
                    if (!peerCert) {
                        tlsSetupError = "TLS handshake succeeded but peer "
                                        "certificate is missing";
                        return false;
                    }
                    X509_free(peerCert);

                    const long verifyResult
                        = SSL_get_verify_result(sess->nativeHandle());
                    if (verifyResult != X509_V_OK) {
                        tlsSetupError = "TLS certificate verification failed: "
                            + std::string(
                                X509_verify_cert_error_string(verifyResult));
                        return false;
                    }
                }
                tlsSession = std::move(sess);
                return true;
            };
            if (reusedConnection && cachedTlsSession_) {
                tlsSession = cachedTlsSession_;
            } else if (!setupTlsForCurrentSocket()) {
                return Result<HttpClientResponse>::failureOwned(
                    SocketError::Unknown, "TLS setup: " + tlsSetupError);
            }
            auto ioBound_sendAll = [&](const char* d, size_t len) -> bool {
                if (tlsSession) {
                    size_t done = 0;
                    while (done < len) {
                        int n = tlsSession->write(
                            d + done, static_cast<int>(len - done));
                        if (n > 0) {
                            done += static_cast<size_t>(n);
                            continue;
                        }
                        const int e = tlsSession->getLastErrorCode(n);
                        if (e == SSL_ERROR_WANT_WRITE
                            || e == SSL_ERROR_WANT_READ)
                            continue;
                        return false;
                    }
                    return true;
                }
                return socket->sendAll(d, len);
            };
            auto ioBound_recv = [&](void* buf, int sz) -> int {
                if (tlsSession) return tlsSession->read(buf, sz);
                return socket->receive(buf, sz);
            };
#else
            auto ioBound_sendAll = [&](const char* d, size_t len) -> bool {
                return socket->sendAll(d, len);
            };
            auto ioBound_recv = [&](void* buf, int sz) -> int {
                return socket->receive(buf, sz);
            };
#endif

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
            if (!hasHeaderCI_(options_.defaultHeaders, "Connection")
                && !hasHeaderCI_(headers, "Connection")) {
                requestBuilder.header("Connection", "keep-alive");
            }

            const bool requestWantsClose
                = headerHasTokenCI_(headers, "Connection", "close")
                || headerHasTokenCI_(
                    options_.defaultHeaders, "Connection", "close");
            if (requestWantsClose) {
                // Honor explicit close semantics across this client instance.
                clearCachedConnection_();
            }

            // Add body for POST/PUT
            if (!body.empty()) {
                requestBuilder.body(body);
            }

            std::string request = requestBuilder.build();
            if (!ioBound_sendAll(request.data(), request.size())) {
                if (reusedConnection) {
                    clearCachedConnection_();

                    ConnectArgs retryArgs{host, port, options_.connectTimeout};
                    auto retrySocketResult = connectAsIpv6
                        ? SocketFactory::createTcpClient(
                              AddressFamily::IPv6, retryArgs)
                        : SocketFactory::createTcpClient(retryArgs);
                    if (!retrySocketResult.isSuccess()) {
                        return Result<HttpClientResponse>::failureOwned(
                            retrySocketResult.error(),
                            "Connection failed to " + host + ":"
                                + std::to_string(port.value()) + " - "
                                + retrySocketResult.message());
                    }

                    socket = std::make_shared<TcpSocket>(
                        std::move(retrySocketResult.value()));
                    reusedConnection = false;
#ifdef AISOCKS_ENABLE_TLS
                    if (!setupTlsForCurrentSocket()) {
                        return Result<HttpClientResponse>::failureOwned(
                            SocketError::Unknown,
                            "TLS setup (retry): " + tlsSetupError);
                    }
#endif
                }

                if (!ioBound_sendAll(request.data(), request.size())) {
                    return Result<HttpClientResponse>::failureOwned(
                        socket->getLastError(),
                        "Failed to send request to " + currentUrl + " - "
                            + socket->getErrorMessage());
                }
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
            bool consumedInterim = false;
            std::function<void()> handleComplete;
            handleComplete = [&]() {
                const int code = parser.response().statusCode;
                // Ignore interim HTTP/1.1 informational responses and keep
                // reading the final response on the same connection.
                if (code >= 100 && code < 200 && code != 101) {
                    consumedInterim = true;
                    std::string remainder = parser.takeRemainingBytes();
                    parser.reset();
                    if (!remainder.empty()) {
                        auto remState
                            = parser.feed(remainder.data(), remainder.size());
                        if (remState == HttpResponseParser::State::Error) {
                            finalResult = Result<HttpClientResponse>::failure(
                                SocketError::Unknown, "Response parse error");
                            haveResult = true;
                            return;
                        }
                        if (remState == HttpResponseParser::State::Complete) {
                            handleComplete();
                            return;
                        }
                    }
                    return;
                }

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
                    socket->setReceiveTimeout(Milliseconds{remainingMs});
                }

                int n = ioBound_recv(buffer, sizeof(buffer));
                if (n < 0) {
                    const auto err = socket->getLastError();
                    if (err == SocketError::Timeout
                        || err == SocketError::WouldBlock)
                        return Result<HttpClientResponse>::failure(
                            SocketError::Timeout, "Request timed out");
                    if (reusedConnection && !retriedReusedConnection) {
                        clearCachedConnection_();
                        retriedReusedConnection = true;
                        --redirectCount;
                        continue;
                    }
                    return Result<HttpClientResponse>::failureOwned(err,
                        "Receive error from " + currentUrl + " - "
                            + socket->getErrorMessage());
                }
                if (n == 0) {
                    parser
                        .feedEof(); // signal EOF for connection-close responses
                    break;
                }

                auto state = parser.feed(buffer, n);

                if (state == HttpResponseParser::State::Error) {
                    if (reusedConnection && !retriedReusedConnection) {
                        clearCachedConnection_();
                        retriedReusedConnection = true;
                        --redirectCount;
                        continue;
                    }
                    return Result<HttpClientResponse>::failure(
                        SocketError::Unknown, "Response parse error");
                }

                if (state == HttpResponseParser::State::Complete) {
                    consumedInterim = false;
                    handleComplete();
                    if (haveResult) {
                        if (finalResult.isSuccess()) {
                            const auto& resp = finalResult.value().response;
                            if (shouldKeepAlive_(resp)) {
                                if (!requestWantsClose) {
                                    cachedHost_ = host;
                                    cachedPort_ = port;
                                    cachedSocket_ = socket;
#ifdef AISOCKS_ENABLE_TLS
                                    cachedTlsSession_ = tlsSession;
#endif
                                } else {
                                    clearCachedConnection_();
                                }
                            } else {
                                clearCachedConnection_();
                            }
                        } else {
                            clearCachedConnection_();
                        }
                        return finalResult;
                    }
                    if (consumedInterim) continue;
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
                if (haveResult) {
                    if (finalResult.isSuccess()) {
                        const auto& resp = finalResult.value().response;
                        if (shouldKeepAlive_(resp)) {
                            if (!requestWantsClose) {
                                cachedHost_ = host;
                                cachedPort_ = port;
                                cachedSocket_ = socket;
#ifdef AISOCKS_ENABLE_TLS
                                cachedTlsSession_ = tlsSession;
#endif
                            } else {
                                clearCachedConnection_();
                            }
                        } else {
                            clearCachedConnection_();
                        }
                    } else {
                        clearCachedConnection_();
                    }
                    return finalResult;
                }
                if (redirectDetected) continue;
            }

            // Connection closed before response was complete.
            if (!redirectDetected) {
                if (reusedConnection && !retriedReusedConnection) {
                    clearCachedConnection_();
                    retriedReusedConnection = true;
                    --redirectCount;
                    continue;
                }
                clearCachedConnection_();
                return Result<HttpClientResponse>::failure(
                    SocketError::ConnectionReset, "Incomplete response");
            }
        }

        return Result<HttpClientResponse>::failure(
            SocketError::Unknown, "Too many redirects");
    }
};

} // namespace aiSocks
