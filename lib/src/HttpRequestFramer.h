// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include <cctype>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>

namespace aiSocks::detail {

enum class ChunkedBodyParseResult {
    NeedMore,
    Complete,
    BadFrame,
    BodyTooLarge
};

inline bool transferEncodingEndsInChunked(std::string_view value) noexcept {
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

inline bool parseChunkSizeWithLimit(
    std::string_view sizeLine, size_t& out, size_t maxDecodedBytes) noexcept {
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

        if (out > (maxDecodedBytes - digit) / 16) return false;
        out = out * 16 + digit;
    }
    return true;
}

inline bool parseContentLengthWithLimit(std::string_view value, size_t& out,
    bool& overflow, size_t maxAllowedBytes = SIZE_MAX) noexcept {
    overflow = false;
    if (value.empty()) return false;
    for (char ch : value) {
        if (ch < '0' || ch > '9') return false;
    }

    unsigned long long parsed = 0;
    auto [ptr, ec]
        = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc{} || ptr != value.data() + value.size()) return false;

    if (parsed > static_cast<unsigned long long>(maxAllowedBytes)
        || parsed > static_cast<unsigned long long>(SIZE_MAX)) {
        overflow = true;
        return false;
    }

    out = static_cast<size_t>(parsed);
    return true;
}

inline ChunkedBodyParseResult parseChunkedBodyWithLimit(std::string_view body,
    size_t maxDecodedBytes, size_t& consumedBytes,
    std::string* decodedOut = nullptr) {
    consumedBytes = 0;
    size_t pos = 0;
    size_t decodedTotal = 0;
    if (decodedOut) decodedOut->clear();

    while (true) {
        const size_t crlfPos = body.find("\r\n", pos);
        if (crlfPos == std::string_view::npos)
            return ChunkedBodyParseResult::NeedMore;

        size_t chunkSize = 0;
        if (!parseChunkSizeWithLimit(
                body.substr(pos, crlfPos - pos), chunkSize, maxDecodedBytes)) {
            return ChunkedBodyParseResult::BadFrame;
        }

        if (chunkSize == 0) {
            const size_t trailerEnd = body.find("\r\n\r\n", crlfPos);
            if (trailerEnd == std::string_view::npos)
                return ChunkedBodyParseResult::NeedMore;
            consumedBytes = trailerEnd + 4;
            return ChunkedBodyParseResult::Complete;
        }

        if (decodedTotal > maxDecodedBytes - chunkSize)
            return ChunkedBodyParseResult::BodyTooLarge;
        decodedTotal += chunkSize;

        const size_t dataStart = crlfPos + 2;
        const size_t dataEnd = dataStart + chunkSize;
        const size_t nextChunk = dataEnd + 2;
        if (nextChunk > body.size()) return ChunkedBodyParseResult::NeedMore;
        if (body[dataEnd] != '\r' || body[dataEnd + 1] != '\n')
            return ChunkedBodyParseResult::BadFrame;

        if (decodedOut) decodedOut->append(body.data() + dataStart, chunkSize);
        pos = nextChunk;
    }
}

} // namespace aiSocks::detail