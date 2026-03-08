#ifndef AISOCKS_HTML_ESCAPE_H
#define AISOCKS_HTML_ESCAPE_H

#include <string>

namespace aiSocks {
namespace HtmlEscape {

    // ---------------------------------------------------------------------------
    // Encode special HTML characters to prevent reflected XSS.
    // Replaces & < > " ' with their named/numeric entities.
    // Pure function — no allocations beyond the returned string.
    // ---------------------------------------------------------------------------
    inline std::string encode(const std::string& input) {
        std::string out;
        out.reserve(input.size());
        for (char c : input) {
            switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                case '\'': out += "&#39;"; break;
                default: out += c; break;
            }
        }
        return out;
    }

} // namespace HtmlEscape
} // namespace aiSocks

#endif // AISOCKS_HTML_ESCAPE_H
