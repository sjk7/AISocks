// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpRequest.h"
#include "UrlCodec.h"

#include <string>
#include <string_view>

namespace aiSocks {

const std::string* HttpRequest::header(std::string_view name) const {
    // Fast path: lowercase into a stack buffer and search via string_view.
    // std::map<std::string, std::string, std::less<>> supports heterogeneous
    // find() in C++17, so no heap allocation occurs for names < 64 chars.
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

std::string HttpRequest::headerOr(
    std::string_view name, std::string fallback) const {
    const std::string* v = header(name);
    return v ? *v : std::move(fallback);
}

// Parses "METHOD SP request-target SP HTTP-version".
// Returns false if the line is malformed (caller should return an invalid req).
static bool parseRequestLine_(std::string_view requestLine, HttpRequest& req) {
    const auto sp1 = requestLine.find(' ');
    if (sp1 == std::string_view::npos) return false;
    const auto sp2 = requestLine.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) return false;

    req.method = std::string(requestLine.substr(0, sp1));
    req.version = std::string(requestLine.substr(sp2 + 1));

    const std::string_view target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
    const auto qmark = target.find('?');
    if (qmark == std::string_view::npos) {
        req.rawPath = std::string(target);
    } else {
        req.rawPath = std::string(target.substr(0, qmark));
        req.queryString = std::string(target.substr(qmark + 1));
    }
    req.path = urlDecodePath(req.rawPath);
    return true;
}

// Parses HTTP header fields from `headerSection` starting after the first CRLF
// (`firstCRLF`).  Keys are lowercased; values have leading/trailing OWS
// trimmed.
static void parseHeaderFields_(
    std::string_view headerSection, size_t firstCRLF, HttpRequest& req) {
    size_t pos = firstCRLF + 2;
    while (pos < headerSection.size()) {
        const auto lineEnd = headerSection.find("\r\n", pos);
        const std::string_view line = (lineEnd == std::string_view::npos)
            ? headerSection.substr(pos)
            : headerSection.substr(pos, lineEnd - pos);

        if (line.empty()) break;

        const auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            const std::string_view rawKey = line.substr(0, colon);
            std::string_view rawVal = line.substr(colon + 1);

            // Lowercase key directly into the map key string — no extra alloc.
            std::string key;
            key.resize(rawKey.size());
            for (size_t i = 0; i < rawKey.size(); ++i)
                key[i] = static_cast<char>(
                    ::tolower(static_cast<unsigned char>(rawKey[i])));

            // Trim leading/trailing OWS from value view — zero allocs.
            const auto valStart = rawVal.find_first_not_of(" \t");
            if (valStart == std::string_view::npos) {
                rawVal = {};
            } else {
                rawVal = rawVal.substr(valStart);
                const auto valEnd = rawVal.find_last_not_of(" \t\r");
                rawVal = (valEnd != std::string_view::npos)
                    ? rawVal.substr(0, valEnd + 1)
                    : std::string_view{};
            }

            req.headers[std::move(key)] = std::string(rawVal);
        }

        if (lineEnd == std::string_view::npos) break;
        pos = lineEnd + 2;
    }
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
                req.queryParams[urlDecode(std::string(pair))] = {};
            } else {
                req.queryParams[urlDecode(std::string(pair.substr(0, eq)))]
                    = urlDecode(std::string(pair.substr(eq + 1)));
            }
        }

        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
}

HttpRequest HttpRequest::parse(const std::string& raw) {
    HttpRequest req;
    const std::string_view sv(raw);

    // Locate header/body separator.
    const auto sep = sv.find("\r\n\r\n");
    const std::string_view headerSection
        = (sep == std::string_view::npos) ? sv : sv.substr(0, sep);
    if (sep != std::string_view::npos)
        req.body = std::string(sv.substr(sep + 4));

    // Split off the request line.
    const auto firstCRLF = headerSection.find("\r\n");
    const std::string_view requestLine = (firstCRLF == std::string_view::npos)
        ? headerSection
        : headerSection.substr(0, firstCRLF);

    if (!parseRequestLine_(requestLine, req)) return req;

    if (firstCRLF != std::string_view::npos)
        parseHeaderFields_(headerSection, firstCRLF, req);

    if (!req.queryString.empty()) parseQueryParams_(req);

    req.valid = true;
    return req;
}

} // namespace aiSocks
