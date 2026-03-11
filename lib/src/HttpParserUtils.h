// Internal HTTP parsing utilities shared by HttpRequest.cpp and
// HttpResponse.cpp. Not part of the public API.
#pragma once

#include <cctype>
#include <string_view>
#include <string>
#include <utility>

namespace aiSocks::detail {

/// Find the header/body separator (\r\n\r\n or bare \n\n, per RFC 7230 §3.5).
/// Search begins at `start`. Returns {sepPos, sepLen}; sepPos == npos if not
/// found yet.
inline std::pair<size_t, size_t> findHeaderBodySep(
    std::string_view s, size_t start = 0) noexcept {
    size_t pos = s.find("\r\n\r\n", start);
    if (pos != std::string_view::npos) return {pos, 4};
    pos = s.find("\n\n", start);
    if (pos != std::string_view::npos) return {pos, 2};
    return {std::string_view::npos, 0};
}

/// Extract the first line from a header section and locate the terminating
/// '\n'.  The returned line has any trailing '\r' stripped.
/// Returns {line, nlPos}. If no '\n' is found, nlPos == npos and line == s.
inline std::pair<std::string_view, size_t> extractFirstLine(
    std::string_view s) noexcept {
    const size_t nlPos = s.find('\n');
    if (nlPos == std::string_view::npos) return {s, std::string_view::npos};
    const size_t lineEnd
        = (nlPos > 0 && s[nlPos - 1] == '\r') ? nlPos - 1 : nlPos;
    return {s.substr(0, lineEnd), nlPos};
}

/// Parse HTTP header fields from `hdr` starting after the '\n' at `firstNL`.
/// Accepts both CRLF and bare LF line endings (RFC 7230 §3.5).
/// Keys are lowercased; values have leading/trailing OWS (SP/HT) trimmed.
/// Calls emit(key, value) for each field.
template <typename EmitFn>
void parseHeaderFields(std::string_view hdr, size_t firstNL, EmitFn&& emit) {
    size_t pos = firstNL + 1;
    while (pos < hdr.size()) {
        const size_t nlPos = hdr.find('\n', pos);
        const size_t nlEnd
            = (nlPos == std::string_view::npos) ? hdr.size() : nlPos;
        const size_t textEnd
            = (nlEnd > pos && hdr[nlEnd - 1] == '\r') ? nlEnd - 1 : nlEnd;
        const std::string_view line = hdr.substr(pos, textEnd - pos);

        if (line.empty()) break;

        const size_t colon = line.find(':');
        if (colon != std::string_view::npos) {
            const std::string_view rawKey = line.substr(0, colon);
            std::string_view rawVal = line.substr(colon + 1);

            std::string key;
            key.resize(rawKey.size());
            for (size_t i = 0; i < rawKey.size(); ++i)
                key[i] = static_cast<char>(
                    ::tolower(static_cast<unsigned char>(rawKey[i])));

            const size_t valStart = rawVal.find_first_not_of(" \t");
            if (valStart == std::string_view::npos) {
                rawVal = {};
            } else {
                rawVal = rawVal.substr(valStart);
                const size_t valEnd = rawVal.find_last_not_of(" \t\r");
                rawVal = (valEnd != std::string_view::npos)
                    ? rawVal.substr(0, valEnd + 1)
                    : std::string_view{};
            }

            emit(std::move(key), rawVal);
        }

        if (nlPos == std::string_view::npos) break;
        pos = nlPos + 1;
    }
}

} // namespace aiSocks::detail
