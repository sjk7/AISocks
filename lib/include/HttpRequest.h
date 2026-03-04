#pragma once
// ---------------------------------------------------------------------------
// HttpRequest.h -- HTTP/1.x request parser
// ---------------------------------------------------------------------------
#include <string>
#include <unordered_map>

namespace aiSocks {

// ---------------------------------------------------------------------------
// HttpRequest
//
// Parses a raw HTTP/1.x request string into its component parts.
//
// Usage:
//   auto req = HttpRequest::parse(rawBytes);
//   if (!req) { /* bad request */ }
//   std::string agent = req.header("user-agent"); // case-insensitive
// ---------------------------------------------------------------------------
struct HttpRequest {
    // ---- parsed fields -------------------------------------------------------
    std::string method;       ///< e.g. "GET", "POST"
    std::string path;         ///< URL-decoded path component
    std::string rawPath;      ///< raw percent-encoded path
    std::string queryString;  ///< raw query string (after '?', before '#')
    std::string version;      ///< e.g. "HTTP/1.1"
    std::string body;         ///< request body (may be empty)

    /// Request headers -- keys are lowercased; values preserve original case.
    std::unordered_map<std::string, std::string> headers;

    /// Query parameters -- both keys and values are URL-decoded.
    std::unordered_map<std::string, std::string> queryParams;

    /// True when the request line was successfully parsed.
    bool valid{false};

    // ---- convenience accessors -----------------------------------------------

    /// Returns a pointer to the header value (nullptr if absent).
    /// The name is lowercased before lookup so callers may pass any case.
    const std::string* header(std::string name) const;

    /// Convenience: returns an empty string if the header is absent.
    std::string headerOr(const std::string& name, std::string fallback = {}) const;

    explicit operator bool() const noexcept { return valid; }

    // ---- parser --------------------------------------------------------------

    /// Parse a raw HTTP/1.x request.  Returns an HttpRequest; check `valid`
    /// (or use `operator bool`) to detect parse failures.
    static HttpRequest parse(const std::string& raw);
};

} // namespace aiSocks
