// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpRequest.h"
#include "HttpParserUtils.h"
#include "UrlCodec.h"

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

    static bool transferEncodingEndsInChunked_(std::string_view value) {
        size_t pos = 0;
        std::string_view last;
        while (pos < value.size()) {
            const size_t comma = value.find(',', pos);
            const size_t end
                = (comma == std::string_view::npos) ? value.size() : comma;
            std::string_view tok = value.substr(pos, end - pos);
            const size_t ts = tok.find_first_not_of(" \t");
            if (ts != std::string_view::npos) tok = tok.substr(ts);
            const size_t te = tok.find_last_not_of(" \t");
            if (te != std::string_view::npos)
                tok = tok.substr(0, te + 1);
            else
                tok = {};
            if (!tok.empty()) last = tok;
            if (comma == std::string_view::npos) break;
            pos = comma + 1;
        }

        if (last.size() != 7) return false;
        return std::tolower(static_cast<unsigned char>(last[0])) == 'c'
            && std::tolower(static_cast<unsigned char>(last[1])) == 'h'
            && std::tolower(static_cast<unsigned char>(last[2])) == 'u'
            && std::tolower(static_cast<unsigned char>(last[3])) == 'n'
            && std::tolower(static_cast<unsigned char>(last[4])) == 'k'
            && std::tolower(static_cast<unsigned char>(last[5])) == 'e'
            && std::tolower(static_cast<unsigned char>(last[6])) == 'd';
    }

    static bool parseChunkSize_(std::string_view sizeLine, size_t& out) {
        const size_t extPos = sizeLine.find(';');
        const std::string_view hex = (extPos == std::string_view::npos)
            ? sizeLine
            : sizeLine.substr(0, extPos);
        if (hex.empty()) return false;
        out = 0;
        for (char c : hex) {
            const unsigned char uc = static_cast<unsigned char>(c);
            size_t digit = 0;
            if (uc >= '0' && uc <= '9')
                digit = static_cast<size_t>(uc - '0');
            else if (uc >= 'a' && uc <= 'f')
                digit = static_cast<size_t>(uc - 'a' + 10);
            else if (uc >= 'A' && uc <= 'F')
                digit = static_cast<size_t>(uc - 'A' + 10);
            else
                return false;

            if (out > (kMaxRequestBodyLen - digit) / 16) return false;
            out = out * 16 + digit;
        }
        return true;
    }

    static bool decodeChunkedBody_(
        std::string_view rawBody, std::string& decodedOut) {
        decodedOut.clear();
        size_t pos = 0;

        while (true) {
            const size_t crlfPos = rawBody.find("\r\n", pos);
            if (crlfPos == std::string_view::npos) return false;

            size_t chunkSize = 0;
            if (!parseChunkSize_(rawBody.substr(pos, crlfPos - pos), chunkSize))
                return false;

            if (chunkSize == 0) {
                const size_t trailerEnd = rawBody.find("\r\n\r\n", crlfPos);
                if (trailerEnd == std::string_view::npos) return false;
                const size_t consumed = trailerEnd + 4;
                return consumed == rawBody.size();
            }

            if (decodedOut.size() > kMaxRequestBodyLen - chunkSize)
                return false;

            const size_t dataStart = crlfPos + 2;
            const size_t dataEnd = dataStart + chunkSize;
            const size_t nextChunk = dataEnd + 2;
            if (nextChunk > rawBody.size()) return false;
            if (rawBody[dataEnd] != '\r' || rawBody[dataEnd + 1] != '\n')
                return false;

            decodedOut.append(rawBody.data() + dataStart, chunkSize);
            pos = nextChunk;
        }
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
        for (unsigned char c : s) {
            if (c < 0x20 || c == 0x7f) return true;
        }
        return false;
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
        if (!transferEncodingEndsInChunked_(*te)) return HttpRequest{};

        std::string decoded;
        if (!decodeChunkedBody_(req.body, decoded)) return HttpRequest{};
        req.body = std::move(decoded);
    } else if (req.body.size() > kMaxRequestBodyLen) {
        return HttpRequest{};
    }

    if (const std::string* cl = req.header("content-length")) {
        // Accept only a non-negative decimal number with no trailing bytes.
        if (cl->empty()) return HttpRequest{};
        uint64_t parsed = 0;
        for (char ch : *cl) {
            if (ch < '0' || ch > '9') return HttpRequest{};
            const uint64_t digit = static_cast<uint64_t>(ch - '0');
            if (parsed > (UINT64_MAX - digit) / 10) return HttpRequest{};
            parsed = parsed * 10 + digit;
        }
        if (parsed > kMaxRequestBodyLen) return HttpRequest{};
        if (parsed != req.body.size()) return HttpRequest{};
    }

    if (!req.queryString.empty()) parseQueryParams_(req);

    req.valid = true;
    return req;
}

} // namespace aiSocks
