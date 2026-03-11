// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpRequest.h"
#include "HttpParserUtils.h"
#include "UrlCodec.h"

#include <string>
#include <string_view>

namespace aiSocks {

const std::string_view* HttpRequest::header(std::string_view name) const {
    // Fast path: lowercase into a stack buffer and search via string_view.
    // std::map<std::string, std::string_view, std::less<>> supports
    // heterogeneous find() in C++17, so no heap allocation occurs for names <
    // 64 chars.
    char sbuf[64];
    if (name.size() < sizeof(sbuf)) {
        for (size_t i = 0; i < name.size(); ++i)
            sbuf[i] = static_cast<char>(
                ::tolower(static_cast<unsigned char>(name[i])));
        auto it = headers.find(std::string_view(sbuf, name.size()));
        return it == headers.end() ? nullptr : &it->second;
    }
    // Fallback for pathologically long names (allocates once).
    std::string key(name.size(), '\0');
    for (size_t i = 0; i < name.size(); ++i)
        key[i]
            = static_cast<char>(::tolower(static_cast<unsigned char>(name[i])));
    auto it = headers.find(key);
    return it == headers.end() ? nullptr : &it->second;
}

std::string_view HttpRequest::headerOr(
    std::string_view name, std::string_view fallback) const {
    const std::string_view* v = header(name);
    return v ? *v : fallback;
}

// Parses "METHOD SP request-target SP HTTP-version".
// Returns false if the line is malformed (caller should return an invalid req).
static bool parseRequestLine_(std::string_view requestLine, HttpRequest& req) {
    const auto sp1 = requestLine.find(' ');
    if (sp1 == std::string_view::npos) return false;
    const auto sp2 = requestLine.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) return false;

    req.method = requestLine.substr(0, sp1);
    req.version = requestLine.substr(sp2 + 1);

    const std::string_view target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
    const auto qmark = target.find('?');
    if (qmark == std::string_view::npos) {
        req.rawPath = target;
    } else {
        req.rawPath = target.substr(0, qmark);
        req.queryString = target.substr(qmark + 1);
    }
    req.path = urlDecodePath(req.rawPath);
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
    if (sep != std::string_view::npos) req.body = sv.substr(sep + sepLen);

    // Split off the request line.
    const auto [requestLine, firstNL] = detail::extractFirstLine(headerSection);

    if (!parseRequestLine_(requestLine, req)) return req;

    if (firstNL != std::string_view::npos)
        detail::parseHeaderFields(headerSection, firstNL,
            [&req](std::string key, std::string_view val) {
                req.headers[std::move(key)] = val;
            });

    if (!req.queryString.empty()) parseQueryParams_(req);

    req.valid = true;
    return req;
}

} // namespace aiSocks
