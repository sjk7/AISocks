// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#include "HttpRequest.h"
#include "UrlCodec.h"

#include <algorithm>
#include <string>

namespace aiSocks {

const std::string* HttpRequest::header(std::string name) const {
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    auto it = headers.find(name);
    return it == headers.end() ? nullptr : &it->second;
}

std::string HttpRequest::headerOr(
    const std::string& name, std::string fallback) const {
    const std::string* v = header(name);
    return v ? *v : std::move(fallback);
}

HttpRequest HttpRequest::parse(const std::string& raw) {
    HttpRequest req;

    // ------------------------------------------------------------------ //
    // 1. Find the header / body separator: first occurrence of \r\n\r\n  //
    // ------------------------------------------------------------------ //
    const auto sep = raw.find("\r\n\r\n");
    const std::string headerSection
        = (sep == std::string::npos) ? raw : raw.substr(0, sep);
    if (sep != std::string::npos)
        req.body = raw.substr(sep + 4);

    // ------------------------------------------------------------------ //
    // 2. Split header section into lines                                  //
    // ------------------------------------------------------------------ //
    const auto firstCRLF = headerSection.find("\r\n");
    const std::string requestLine = (firstCRLF == std::string::npos)
        ? headerSection
        : headerSection.substr(0, firstCRLF);

    // ------------------------------------------------------------------ //
    // 3. Parse the request line: METHOD SP request-target SP HTTP-version //
    // ------------------------------------------------------------------ //
    {
        const auto sp1 = requestLine.find(' ');
        if (sp1 == std::string::npos) return req;

        const auto sp2 = requestLine.find(' ', sp1 + 1);
        if (sp2 == std::string::npos) return req;

        req.method  = requestLine.substr(0, sp1);
        req.version = requestLine.substr(sp2 + 1);

        const std::string target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
        const auto qmark = target.find('?');
        if (qmark == std::string::npos) {
            req.rawPath = target;
        } else {
            req.rawPath     = target.substr(0, qmark);
            req.queryString = target.substr(qmark + 1);
        }
        req.path = urlDecode(req.rawPath);
    }

    // ------------------------------------------------------------------ //
    // 4. Parse header fields                                              //
    // ------------------------------------------------------------------ //
    if (firstCRLF != std::string::npos) {
        size_t pos = firstCRLF + 2;
        while (pos < headerSection.size()) {
            const auto lineEnd = headerSection.find("\r\n", pos);
            const std::string line = (lineEnd == std::string::npos)
                ? headerSection.substr(pos)
                : headerSection.substr(pos, lineEnd - pos);

            if (line.empty()) break;

            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key   = line.substr(0, colon);
                std::string value = line.substr(colon + 1);

                std::transform(key.begin(), key.end(), key.begin(),
                    [](unsigned char c) { return static_cast<char>(::tolower(c)); });

                const auto valStart = value.find_first_not_of(" \t");
                if (valStart == std::string::npos) {
                    value.clear();
                } else {
                    value = value.substr(valStart);
                    const auto valEnd = value.find_last_not_of(" \t\r");
                    if (valEnd != std::string::npos)
                        value = value.substr(0, valEnd + 1);
                    else
                        value.clear();
                }

                req.headers[std::move(key)] = std::move(value);
            }

            if (lineEnd == std::string::npos) break;
            pos = lineEnd + 2;
        }
    }

    // ------------------------------------------------------------------ //
    // 5. Parse query parameters                                           //
    // ------------------------------------------------------------------ //
    if (!req.queryString.empty()) {
        size_t pos = 0;
        const auto& qs = req.queryString;
        while (pos <= qs.size()) {
            const auto amp = qs.find('&', pos);
            const std::string pair = (amp == std::string::npos)
                ? qs.substr(pos)
                : qs.substr(pos, amp - pos);

            if (!pair.empty()) {
                const auto eq = pair.find('=');
                if (eq == std::string::npos) {
                    req.queryParams[urlDecode(pair)] = {};
                } else {
                    req.queryParams[urlDecode(pair.substr(0, eq))]
                        = urlDecode(pair.substr(eq + 1));
                }
            }

            if (amp == std::string::npos) break;
            pos = amp + 1;
        }
    }

    req.valid = true;
    return req;
}

} // namespace aiSocks
