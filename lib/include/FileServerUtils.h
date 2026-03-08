#ifndef AISOCKS_FILE_SERVER_UTILS_H
#define AISOCKS_FILE_SERVER_UTILS_H

#include <cstdint>
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
        struct HexTable {
            uint8_t v[256];
        };
        static const HexTable fromHex = []() noexcept {
            HexTable t{};
            for (int j = 0; j < 256; ++j) t.v[j] = 0xFF;
            for (int i = 0; i < 10; ++i)
                t.v[static_cast<unsigned>('0' + i)] = static_cast<uint8_t>(i);
            for (int i = 0; i < 6; ++i) {
                t.v[static_cast<unsigned>('A' + i)] = static_cast<uint8_t>(10 + i);
                t.v[static_cast<unsigned>('a' + i)] = static_cast<uint8_t>(10 + i);
            }
            return t;
        }();

        std::string out;
        out.reserve(src.size());
        for (size_t i = 0, n = src.size(); i < n; ++i) {
            const unsigned char c = static_cast<unsigned char>(src[i]);
            if (c == '%' && i + 2 < n) {
                const uint8_t hi
                    = fromHex.v[static_cast<unsigned char>(src[i + 1])];
                const uint8_t lo
                    = fromHex.v[static_cast<unsigned char>(src[i + 2])];
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
    // securityHeadersBlock
    //
    // Returns a compile-time constant string containing the four HTTP security
    // response headers, each CRLF-terminated, ready to append to any response
    // builder.  The caller is responsible for checking the enabled flag.
    //
    //   X-Content-Type-Options: nosniff
    //   X-Frame-Options: DENY
    //   Content-Security-Policy: default-src 'self' ...
    //   Referrer-Policy: no-referrer
    // ---------------------------------------------------------------------------
    inline const char* securityHeadersBlock() noexcept {
        return "X-Content-Type-Options: nosniff\r\n"
               "X-Frame-Options: DENY\r\n"
               "Content-Security-Policy: default-src 'self'; style-src 'self' "
               "'unsafe-inline'; script-src 'self' 'unsafe-inline'\r\n"
               "Referrer-Policy: no-referrer\r\n";
    }

} // namespace FileServerUtils
} // namespace aiSocks

#endif // AISOCKS_FILE_SERVER_UTILS_H
