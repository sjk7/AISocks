#ifndef AISOCKS_FILE_SERVER_UTILS_H
#define AISOCKS_FILE_SERVER_UTILS_H

#include "FileIO.h" // StringBuilder
#include <array>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>

#ifdef _WIN32
#include <time.h> // gmtime_s
#endif

namespace aiSocks {

namespace FileServerUtils {

    // ---------------------------------------------------------------------------
    // urlDecodePath
    //
    // Decodes %xx percent-encoding in a URL path.  Non-hex-pair occurrences of
    // '%' are passed through unchanged so partial / malformed sequences don't
    // corrupt the output.
    //
    // Uses a static 256-entry lookup table built once at first call so repeated
    // calls pay no initialisation cost.
    // ---------------------------------------------------------------------------
    inline std::string urlDecodePath(const std::string& src) {
        static const auto fromHex = []() noexcept {
            std::array<uint8_t, 256> t{};
            t.fill(0xFF);
            for (int i = 0; i < 10; ++i)
                t[static_cast<unsigned>('0' + i)] = static_cast<uint8_t>(i);
            for (int i = 0; i < 6; ++i) {
                t[static_cast<unsigned>('A' + i)]
                    = static_cast<uint8_t>(10 + i);
                t[static_cast<unsigned>('a' + i)]
                    = static_cast<uint8_t>(10 + i);
            }
            return t;
        }();

        std::string out;
        out.reserve(src.size());
        for (size_t i = 0, n = src.size(); i < n; ++i) {
            const unsigned char c = static_cast<unsigned char>(src[i]);
            if (c == '%' && i + 2 < n) {
                const uint8_t hi
                    = fromHex[static_cast<unsigned char>(src[i + 1])];
                const uint8_t lo
                    = fromHex[static_cast<unsigned char>(src[i + 2])];
                if (hi != 0xFF && lo != 0xFF) {
                    out += static_cast<char>((hi << 4) | lo);
                    i += 2;
                    continue;
                }
            }
            out += static_cast<char>(c);
        }
        return out;
    }

    // ---------------------------------------------------------------------------
    // getFileExtension
    //
    // Returns the last dot-prefixed suffix of filePath (e.g. ".html"),
    // or an empty string if there is no extension.
    // ---------------------------------------------------------------------------
    inline std::string getFileExtension(const std::string& filePath) {
        const size_t dotPos = filePath.find_last_of('.');
        if (dotPos != std::string::npos && dotPos < filePath.length() - 1)
            return filePath.substr(dotPos);
        return {};
    }

    // ---------------------------------------------------------------------------
    // formatHttpDate
    //
    // Formats fileTime as "Day, DD Mon YYYY HH:MM:SS GMT"
    // (the RFC 7231 / HTTP-date preferred format).
    //
    // Uses gmtime_s on Windows (re-entrant) and gmtime elsewhere.
    // ---------------------------------------------------------------------------
    inline std::string formatHttpDate(time_t fileTime) {
        char buffer[32];
#ifdef _WIN32
        struct tm timeinfo = {};
        gmtime_s(&timeinfo, &fileTime);
        strftime(
            buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &timeinfo);
#else
        struct tm* timeinfo = gmtime(&fileTime);
        strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", timeinfo);
#endif
        return std::string(buffer);
    }

    // ---------------------------------------------------------------------------
    // addSecurityHeaders
    //
    // Appends four HTTP security response headers to `response` when
    // `enabled` is true.  The headers are:
    //   - X-Content-Type-Options: nosniff (prevents MIME-sniffing attacks)
    //   - X-Frame-Options: DENY (blocks framing by any origin)
    //   - Content-Security-Policy: default-src 'self' ... (restricts resource
    //   loads)
    //   - Referrer-Policy: no-referrer (omits Referer header on outbound
    //   navigations)
    // ---------------------------------------------------------------------------
    inline void addSecurityHeaders(StringBuilder& response, bool enabled) {
        if (!enabled) return;
        response.append("X-Content-Type-Options: nosniff\r\n");
        response.append("X-Frame-Options: DENY\r\n");
        response.append(
            "Content-Security-Policy: default-src 'self'; style-src 'self' "
            "'unsafe-inline'; script-src 'self' 'unsafe-inline'\r\n");
        response.append("Referrer-Policy: no-referrer\r\n");
    }

} // namespace FileServerUtils
} // namespace aiSocks

#endif // AISOCKS_FILE_SERVER_UTILS_H
