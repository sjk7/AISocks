// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpRequest.h"
#include "HttpRequestFramer.h"
#include "HttpParserUtils.h"
#include "UrlCodec.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace aiSocks {

namespace {
    constexpr size_t kMaxRequestLineLen = 4096;
    constexpr size_t kMaxQueryStringLen = 8192;
    constexpr size_t kMaxRequestHeaderSectionLen = 16 * 1024;
    constexpr size_t kMaxRequestBodyLen = 16 * 1024 * 1024;

    static bool decodeChunkedBody_(
        std::string_view rawBody, std::string& decodedOut) {
        size_t consumed = 0;
        const auto result = detail::parseChunkedBodyWithLimit(
            rawBody, kMaxRequestBodyLen, consumed, &decodedOut);
        return result == detail::ChunkedBodyParseResult::Complete
            && consumed == rawBody.size();
    }
} // namespace

const std::string* HttpRequest::header(std::string_view name) const {
    return detail::lookupHeaderCI(headers, name);
}

std::string_view HttpRequest::headerOr(
    std::string_view name, std::string_view fallback) const {
    const std::string* v = header(name);
    return v ? std::string_view(*v) : fallback;
}

// Parses "METHOD SP request-target SP HTTP-version".
// Returns false if the line is malformed (caller should return an invalid req).
static bool parseRequestLine_(std::string_view requestLine, HttpRequest& req) {
    if (requestLine.empty() || requestLine.size() > kMaxRequestLineLen)
        return false;

    const auto sp1 = requestLine.find(' ');
    if (sp1 == std::string_view::npos) return false;
    const auto sp2 = requestLine.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) return false;
    if (requestLine.find(' ', sp2 + 1) != std::string_view::npos) return false;
    if (sp1 == 0 || sp2 <= sp1 + 1 || sp2 + 1 >= requestLine.size())
        return false;

    req.method = requestLine.substr(0, sp1);
    req.version = requestLine.substr(sp2 + 1);

    // Reject embedded control bytes in request-line tokens.
    auto hasCtl = [](std::string_view s) {
        return std::any_of(s.begin(), s.end(),
            [](unsigned char c) { return c < 0x20 || c == 0x7f; });
    };
    if (hasCtl(req.method) || hasCtl(req.version)) return false;
    if (req.version != "HTTP/1.0" && req.version != "HTTP/1.1") return false;

    const std::string_view target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
    if (target.empty()) return false;
    if (hasCtl(target)) return false;
    const auto qmark = target.find('?');
    if (qmark == std::string_view::npos) {
        req.rawPath = target;
    } else {
        req.rawPath = target.substr(0, qmark);
        req.queryString = target.substr(qmark + 1);
        if (req.queryString.size() > kMaxQueryStringLen) return false;
    }

    const bool isAsterisk = req.rawPath == "*";
    const bool isAbsolute = req.rawPath.find("://") != std::string::npos;
    const bool isOrigin = !req.rawPath.empty() && req.rawPath.front() == '/';
    const bool isAuthority = !req.rawPath.empty() && !isAsterisk && !isAbsolute
        && req.rawPath.front() != '/';

    // Allowed request-target forms:
    // - origin-form (starts with '/')
    // - absolute-form (contains scheme://)
    // - asterisk-form ('*') for OPTIONS
    // - authority-form for CONNECT
    if (req.method == "CONNECT") {
        if (!isAuthority || !req.queryString.empty()) return false;
    } else if (isAsterisk) {
        if (req.method != "OPTIONS" || !req.queryString.empty()) return false;
    } else {
        if (isAuthority) return false;
        if (!isOrigin && !isAbsolute) return false;
    }

    req.path = urlDecodePath(req.rawPath);
    if (req.path.empty()) return false;
    if (isOrigin && req.path.front() != '/') return false;
    return true;
}

// Splits `req.queryString` into `req.queryParams` (URL-decoded key=value
// pairs separated by '&').
static void parseQueryParams_(HttpRequest& req) {
    size_t pos = 0;
    const std::string_view qs(req.queryString);
    while (pos <= qs.size()) {
        const auto amp = qs.find('&', pos);
        const std::string_view pair = (amp == std::string_view::npos)
            ? qs.substr(pos)
            : qs.substr(pos, amp - pos);

        if (!pair.empty()) {
            const auto eq = pair.find('=');
            if (eq == std::string_view::npos) {
                req.queryParams[urlDecode(pair)] = {};
            } else {
                req.queryParams[urlDecode(pair.substr(0, eq))]
                    = urlDecode(pair.substr(eq + 1));
            }
        }

        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
}

HttpRequest HttpRequest::parse(std::string_view raw) {
    HttpRequest req;
    const std::string_view sv(raw);

    // Locate header/body separator (\r\n\r\n or bare \n\n — RFC 7230 §3.5).
    const auto [sep, sepLen] = detail::findHeaderBodySep(sv);
    const std::string_view headerSection
        = (sep == std::string_view::npos) ? sv : sv.substr(0, sep);
    if (headerSection.size() > kMaxRequestHeaderSectionLen) return req;
    if (sep != std::string_view::npos) req.body = sv.substr(sep + sepLen);

    // Split off the request line.
    const auto [requestLine, firstNL] = detail::extractFirstLine(headerSection);

    if (!parseRequestLine_(requestLine, req)) return req;

    if (firstNL != std::string_view::npos)
        detail::parseHeaderFields(headerSection, firstNL,
            [&req](std::string key, std::string_view val) {
                req.headers[std::move(key)] = std::string(val);
            });

    // Disallow framing ambiguity in requests: both Transfer-Encoding and
    // Content-Length present at once is rejected.
    if (req.header("transfer-encoding") && req.header("content-length"))
        return HttpRequest{};

    if (const std::string* te = req.header("transfer-encoding")) {
        if (!detail::transferEncodingEndsInChunked(*te)) return HttpRequest{};

        std::string decoded;
        if (!decodeChunkedBody_(req.body, decoded)) return HttpRequest{};
        req.body = std::move(decoded);
    } else if (req.body.size() > kMaxRequestBodyLen) {
        return HttpRequest{};
    }

    if (const std::string* cl = req.header("content-length")) {
        size_t parsed = 0;
        bool overflow = false;
        if (!detail::parseContentLengthWithLimit(
                *cl, parsed, overflow, kMaxRequestBodyLen)) {
            return HttpRequest{};
        }
        if (parsed != req.body.size()) return HttpRequest{};
    }

    if (!req.queryString.empty()) parseQueryParams_(req);

    req.valid = true;
    return req;
}

} // namespace aiSocks
