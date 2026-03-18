// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "UrlCodec.h"

#include <array>
#include <cstdint>

namespace aiSocks {

namespace {
    // Shared hex-digit lookup table for both urlDecode and urlDecodePath.
    // 0xFF means «not a hex digit».
    const std::array<uint8_t, 256> kFromHex = []() noexcept {
        std::array<uint8_t, 256> t{};
        t.fill(0xFF);
        for (int i = 0; i < 10; ++i)
            t[static_cast<unsigned>('0' + i)] = static_cast<uint8_t>(i);
        for (int i = 0; i < 6; ++i) {
            t[static_cast<unsigned>('A' + i)] = static_cast<uint8_t>(10 + i);
            t[static_cast<unsigned>('a' + i)] = static_cast<uint8_t>(10 + i);
        }
        return t;
    }();
    // Shared decode implementation: plusIsSpace=true for query strings,
    // false for URL paths (RFC 3986 — '+' is literal in a path).
    static std::string urlDecodeImpl(std::string_view src, bool plusIsSpace) {
        std::string out;
        out.reserve(src.size());
        for (size_t i = 0, n = src.size(); i < n; ++i) {
            const unsigned char c = static_cast<unsigned char>(src[i]);
            if (c == '%' && i + 2 < n) {
                const uint8_t hi = kFromHex[static_cast<unsigned char>(src[i + 1])];
                const uint8_t lo = kFromHex[static_cast<unsigned char>(src[i + 2])];
                if (hi != 0xFF && lo != 0xFF) {
                    const unsigned char decoded = static_cast<unsigned char>((hi << 4) | lo);
                    // Security: Reject null bytes (0x00) as they are invalid in URLs
                    // and commonly used in directory traversal attacks
                    if (decoded == 0x00) {
                        // Skip null byte entirely - treat as invalid encoding
                        i += 2;
                        continue;
                    }
                    out += static_cast<char>(decoded);
                    i += 2;
                    continue;
                }
            } else if (plusIsSpace && c == '+') {
                out += ' ';
                continue;
            }
            out += static_cast<char>(c);
        }
        return out;
    }
} // namespace

std::string urlEncode(std::string_view src) {
    static constexpr char hex[] = "0123456789ABCDEF";
    static const auto safe = []() noexcept {
        std::array<bool, 256> t{};
        for (int c = 'A'; c <= 'Z'; ++c) t[static_cast<unsigned>(c)] = true;
        for (int c = 'a'; c <= 'z'; ++c) t[static_cast<unsigned>(c)] = true;
        for (int c = '0'; c <= '9'; ++c) t[static_cast<unsigned>(c)] = true;
        t[static_cast<unsigned>('-')] = true;
        t[static_cast<unsigned>('_')] = true;
        t[static_cast<unsigned>('.')] = true;
        t[static_cast<unsigned>('~')] = true;
        return t;
    }();

    size_t outSize = src.size();
    for (auto ch : src)
        if (!safe[static_cast<unsigned char>(ch)]) outSize += 2;

    std::string out;
    out.reserve(outSize);
    for (auto ch : src) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (safe[c]) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

std::string urlDecode(std::string_view src) {
    return urlDecodeImpl(src, true);
}

std::string urlDecodePath(std::string_view src) {
    return urlDecodeImpl(src, false);
}

} // namespace aiSocks
