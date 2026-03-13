// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once
// ---------------------------------------------------------------------------
// HttpResponse.h -- HTTP/1.x response parser (incremental / feed-based)
// ---------------------------------------------------------------------------
//
// Usage (single-shot):
//
//   HttpResponseParser parser;
//   while (bytes_available) {
//       auto state = parser.feed(buf, n);
//       if (state == HttpResponseParser::State::Complete) break;
//       if (state == HttpResponseParser::State::Error)    { /* bad response */
//       break; }
//   }
//   const HttpResponse& resp = parser.response();
//   if (resp) {
//       // resp.statusCode, resp.version(), resp.body(),
//       resp.header("content-type"), ...
//   }
//
// After a keep-alive exchange call parser.reset() before feeding the next
// response.
//
// Lifetime:
//   HttpResponse owns all its data (std::string fields).  Copies are
//   independent and remain valid after the parser is reset() or destroyed.
//
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "HeaderMap.h"

namespace aiSocks {

class HttpResponseParser;

// ---------------------------------------------------------------------------
// HttpResponse
//
// Populated incrementally by HttpResponseParser.  Public interface exposes
// only string_views into the parser's stable internal buffers.
//
// Fields/accessors are valid as follows:
//   - statusCode, version(), statusText()  — once isHeadersComplete()
//   - header(name), headers()              — once isHeadersComplete()
//   - body()                               — once isComplete()
//   - valid / operator bool                — once isComplete()
// ---------------------------------------------------------------------------
class HttpResponse {
    public:
    // Set once the status line has been parsed (isHeadersComplete()).
    int statusCode{0};

    /// HTTP version string, e.g. "HTTP/1.1"
    std::string_view version() const noexcept { return version_; }

    /// Reason phrase, e.g. "OK", "Not Found"
    std::string_view statusText() const noexcept { return statusText_; }

    /// Response body — valid (and non-empty for non-empty bodies) once
    /// isComplete().  Empty string_view if called earlier.
    std::string_view body() const noexcept { return body_; }

    /// Case-insensitive header lookup.  Returns nullptr if absent.
    const std::string* header(std::string_view name) const;

    /// Direct access to the parsed header map.
    /// Keys are lowercased; values are owned strings.
    const HeaderMap& headers() const noexcept { return headers_; }

    /// True once isComplete() — all headers and the full body are available.
    bool valid{false};
    explicit operator bool() const noexcept { return valid; }

    class Builder {
        public:
        Builder& status(int code, std::string_view reason = "") {
            code_ = code;
            reason_ = reason;
            return *this;
        }

        Builder& header(std::string_view name, std::string_view value) {
            headers_.emplace_back(std::string(name), std::string(value));
            return *this;
        }

        Builder& contentType(std::string_view type) {
            return header("Content-Type", type);
        }

        Builder& body(std::string body) {
            body_ = std::move(body);
            return *this;
        }

        Builder& keepAlive(bool keep) {
            keepAlive_ = keep;
            return *this;
        }

        std::string build() const;

        private:
        int code_{200};
        std::string reason_;
        std::vector<std::pair<std::string, std::string>> headers_;
        std::string body_;
        bool keepAlive_{true};

        std::string_view getReason_(int code) const;
    };

    static Builder builder() { return Builder(); }

    private:
    friend class HttpResponseParser;

    std::string version_;
    std::string statusText_;
    std::string body_;
    HeaderMap headers_;
};

// ---------------------------------------------------------------------------
// HttpResponseParser
// ---------------------------------------------------------------------------
class HttpResponseParser {
    public:
    static constexpr size_t kMaxHeaderSectionLen = 64 * 1024;
    static constexpr size_t kMaxBodyLen = 64 * 1024 * 1024;
    static constexpr size_t kMaxChunkSize = 16 * 1024 * 1024;

    enum class State {
        Incomplete, ///< More data needed
        HeadersComplete, ///< Status line + all headers parsed; body may be
                         ///< incomplete
        Complete, ///< Full response (headers + body) received
        Error ///< Parse error; call reset() before reuse
    };

    HttpResponseParser() {
        // Reserve space to reduce reallocations during incremental feeding.
        inBuf_.reserve(16384);
        bodyBuf_.reserve(16384);
    }
    ~HttpResponseParser() = default;

    // Non-copyable — large internal buffers should not be accidentally copied
    HttpResponseParser(const HttpResponseParser&) = delete;
    HttpResponseParser& operator=(const HttpResponseParser&) = delete;

    HttpResponseParser(HttpResponseParser&&) = default;
    HttpResponseParser& operator=(HttpResponseParser&&) = default;

    // ---- feed interface -------------------------------------------------

    /// Feed raw bytes.  Returns current State after processing.
    /// Can be called repeatedly; bytes are internally accumulated.
    State feed(const char* data, size_t len);

    /// Signal peer connection close (for responses with no Content-Length and
    /// no Transfer-Encoding, where body ends at EOF).
    State feedEof();

    // ---- state queries --------------------------------------------------

    State state() const noexcept { return state_; }

    bool isHeadersComplete() const noexcept {
        return state_ == State::HeadersComplete || state_ == State::Complete;
    }
    bool isComplete() const noexcept { return state_ == State::Complete; }
    bool isError() const noexcept { return state_ == State::Error; }

    // ---- result ---------------------------------------------------------

    /// Access the parsed response.
    /// header() / statusCode / version() are valid from isHeadersComplete().
    /// body() and valid are set on isComplete().
    const HttpResponse& response() const noexcept { return response_; }

    /// Extract bytes that were received beyond the currently completed
    /// response frame (e.g. pipelined or coalesced next response bytes).
    /// Returns an empty string if there is no buffered remainder.
    std::string takeRemainingBytes();

    // ---- reset ----------------------------------------------------------

    /// Reset all state.  Call between keep-alive responses.
    void reset();

    private:
    // Internal body mode (set once headers are parsed)
    enum class BodyMode { Unknown, ContentLength, Chunked, ConnectionClose };

    // ---- internal methods -----------------------------------------------
    bool tryParseHeaders_();
    State processBody_();
    State processChunked_();
    void markComplete_();
    void markError_();
    State advanceAfterHeaders_();
    void determineBodyMode_();
    static bool parseHexSize_(std::string_view hexStr, size_t& out) noexcept;
    // Strips chunk-extensions (RFC 7230 §4.1.1) then delegates to
    // parseHexSize_.
    static bool parseChunkSize_(
        std::string_view sizeLine, size_t& out) noexcept;

    // ---- buffers --------------------------------------------------------
    // inBuf_      — accumulates raw bytes until \r\n\r\n is found
    // headerBuf_  — frozen copy of the header section (including status line)
    // bodyBuf_    — raw body bytes: Content-Length payload, or raw chunked data
    // decodedBody_ — TE:chunked only — assembled decoded body
    std::string inBuf_;
    std::string headerBuf_;
    std::string bodyBuf_;
    std::string decodedBody_;

    // ---- parser state ---------------------------------------------------
    State state_{State::Incomplete};
    bool headersParsed_{false};
    size_t headerScanPos_{0}; // incremental \r\n\r\n scan in inBuf_

    BodyMode bodyMode_{BodyMode::Unknown};
    int64_t contentLength_{-1}; // -1 = not present
    size_t chunkScanPos_{0}; // scan frontier for chunk parsing in bodyBuf_

    HttpResponse response_;
};

} // namespace aiSocks
