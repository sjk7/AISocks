// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_URL_CODEC_H
#define AISOCKS_URL_CODEC_H

// ---------------------------------------------------------------------------
// urlEncode / urlDecode
//
// RFC 3986 percent-encoding.  Unreserved characters (A-Z a-z 0-9 - _ . ~)
// pass through unchanged; everything else is encoded as %XX (uppercase hex).
//
// urlDecode additionally treats '+' as space (form-encoding convention) and
// passes invalid/truncated %XX sequences through verbatim.
//
// Both functions pre-size their output string to avoid reallocations and use
// 256-entry lookup tables initialised once via IIFEs.
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <string>

namespace aiSocks {

// Percent-encode src.  Unreserved chars (A-Z a-z 0-9 - _ . ~) pass through.
inline std::string urlEncode(const std::string& src) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    static const auto kSafe = []() noexcept {
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
    for (unsigned char c : src)
        if (!kSafe[c]) outSize += 2;

    std::string out;
    out.reserve(outSize);
    for (unsigned char c : src) {
        if (kSafe[c]) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

// Decode a percent-encoded string.  Invalid sequences are passed verbatim.
inline std::string urlDecode(const std::string& src) {
    static const auto kFromHex = []() noexcept {
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

    std::string out;
    out.reserve(src.size());
    for (size_t i = 0, n = src.size(); i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        if (c == '%' && i + 2 < n) {
            const uint8_t hi = kFromHex[static_cast<unsigned char>(src[i + 1])];
            const uint8_t lo = kFromHex[static_cast<unsigned char>(src[i + 2])];
            if (hi != 0xFF && lo != 0xFF) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        } else if (c == '+') {
            out += ' ';
            continue;
        }
        out += static_cast<char>(c);
    }
    return out;
}

} // namespace aiSocks

#endif // AISOCKS_URL_CODEC_H
