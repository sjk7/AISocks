// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include <cctype>
#include <cstddef>
#include <string_view>

namespace aiSocks::detail {

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

} // namespace aiSocks::detail