// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once
// ---------------------------------------------------------------------------
// HttpRequest.h -- HTTP/1.x request parser
// ---------------------------------------------------------------------------
#include <map>
#include <string>
#include <string_view>

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
//
// WARNING: When HttpRequest is parsed from a string_view, it holds views into
// the original buffer. The caller must ensure the buffer outlives this object.
// ---------------------------------------------------------------------------
struct HttpRequest {
    // ---- parsed fields
    // -------------------------------------------------------
    std::string_view method; ///< e.g. "GET", "POST"
    std::string path; ///< URL-decoded path component
    std::string_view rawPath; ///< raw percent-encoded path
    std::string_view queryString; ///< raw query string (after '?', before '#')
    std::string_view version; ///< e.g. "HTTP/1.1"
    std::string_view body; ///< request body (may be empty)

    /// Request headers -- keys are lowercased; values preserve original case.
    /// Uses std::less<> (transparent comparator) so find() accepts string_view
    /// and const char* directly without allocating a temporary std::string.
    std::map<std::string, std::string_view, std::less<>> headers;

    /// Query parameters -- both keys and values are URL-decoded.
    std::map<std::string, std::string, std::less<>> queryParams;

    /// True when the request line was successfully parsed.
    bool valid{false};

    // ---- convenience accessors
    // -----------------------------------------------

    /// Returns a pointer to the header value (nullptr if absent).
    /// The name is lowercased before lookup so callers may pass any case.
    const std::string_view* header(std::string_view name) const;

    /// Convenience: returns an empty string if the header is absent.
    std::string_view headerOr(
        std::string_view name, std::string_view fallback = {}) const;

    explicit operator bool() const noexcept { return valid; }

    // ---- parser
    // --------------------------------------------------------------

    /// Parse a raw HTTP/1.x request.  Returns an HttpRequest; check `valid`
    /// (or use `operator bool`) to detect parse failures.
    /// Note: Holds views into `raw`.
    static HttpRequest parse(std::string_view raw);
};

} // namespace aiSocks
