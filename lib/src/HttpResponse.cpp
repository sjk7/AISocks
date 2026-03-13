// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpResponse.h"
#include "HttpParserUtils.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>

namespace aiSocks {

// ---------------------------------------------------------------------------
// HttpResponse
// ---------------------------------------------------------------------------

const std::string* HttpResponse::header(std::string_view name) const {
    return detail::lookupHeaderCI(headers_, name);
}

// ---------------------------------------------------------------------------
// HttpResponseParser — helpers
// ---------------------------------------------------------------------------

void HttpResponseParser::markError_() {
    state_ = State::Error;
}

// static
bool HttpResponseParser::parseHexSize_(
    std::string_view hexStr, size_t& out) noexcept {
    if (hexStr.empty()) return false;
    out = 0;
    for (char c : hexStr) {
        const auto uc = static_cast<unsigned char>(c);
        size_t digit = 0;
        if (uc >= '0' && uc <= '9')
            digit = uc - '0';
        else if (uc >= 'a' && uc <= 'f')
            digit = uc - 'a' + 10;
        else if (uc >= 'A' && uc <= 'F')
            digit = uc - 'A' + 10;
        else
            return false;
        if (out > (kMaxChunkSize - digit) / 16) return false;
        out = out * 16 + digit;
    }
    return out <= kMaxChunkSize;
}

// static — Strips chunk-extensions (everything after ';', per RFC 7230 §4.1.1)
// then delegates to parseHexSize_.  Returns false on malformed input.
bool HttpResponseParser::parseChunkSize_(
    std::string_view sizeLine, size_t& out) noexcept {
    const size_t extPos = sizeLine.find(';');
    const std::string_view hexStr = (extPos == std::string_view::npos)
        ? sizeLine
        : sizeLine.substr(0, extPos);
    return parseHexSize_(hexStr, out);
}

void HttpResponseParser::markComplete_() {
    response_.valid = true;
    state_ = State::Complete;
}

std::string HttpResponseParser::takeRemainingBytes() {
    if (state_ != State::Complete || !headersParsed_) return {};

    if (bodyMode_ == BodyMode::ContentLength && contentLength_ >= 0) {
        const size_t consumed = static_cast<size_t>(contentLength_);
        if (bodyBuf_.size() > consumed) return bodyBuf_.substr(consumed);
        return {};
    }

    if (bodyMode_ == BodyMode::Chunked) {
        if (chunkScanPos_ < bodyBuf_.size())
            return bodyBuf_.substr(chunkScanPos_);
        return {};
    }

    if (bodyMode_ == BodyMode::ConnectionClose) return {};

    // 1xx / 204 / 304 complete in headers phase; bodyBuf_ holds any
    // coalesced bytes for the next response.
    return bodyBuf_;
}

// ---------------------------------------------------------------------------
// tryParseHeaders_
//
// Called once enough bytes are in inBuf_ to contain \r\n\r\n.
// Freezes headerBuf_, moves leftover body bytes to bodyBuf_,
// populates response_.statusCode / version_ / statusText_ / headers_.
// Returns false if the status line is malformed (caller sets Error state).
// ---------------------------------------------------------------------------
bool HttpResponseParser::tryParseHeaders_() {
    // Incremental scan for separator (\r\n\r\n or bare \n\n, RFC 7230 §3.5) ---
    // //
    const size_t start = headerScanPos_ >= 3 ? headerScanPos_ - 3 : 0;
    const auto [sepPos, sepLen]
        = detail::findHeaderBodySep(std::string_view(inBuf_), start);
    if (sepPos == std::string_view::npos) {
        // Not yet — advance scan frontier (keep 3 tail bytes for overlap)
        headerScanPos_ = inBuf_.size() >= 3 ? inBuf_.size() - 3 : 0;
        return false; // really means "not ready yet" — caller checks bool
    }
    if (sepPos > kMaxHeaderSectionLen) {
        markError_();
        return false;
    }

    // Freeze header section
    headerBuf_ = inBuf_.substr(0, sepPos);
    // Any bytes after the separator belong to the body
    if (sepPos + sepLen < inBuf_.size())
        bodyBuf_ = inBuf_.substr(sepPos + sepLen);
    inBuf_.clear();
    // Do NOT shrink_to_fit here: keeping the capacity avoids a reallocation
    // on the next keep-alive request that feeds into inBuf_.

    const std::string_view hdr(headerBuf_);

    // Parse status line: HTTP-version SP status-code SP reason-phrase
    const auto [statusLine, firstNL] = detail::extractFirstLine(hdr);

    const size_t sp1 = statusLine.find(' ');
    if (sp1 == std::string_view::npos) {
        markError_();
        return false;
    }

    const size_t sp2 = statusLine.find(' ', sp1 + 1);
    // reason-phrase may be absent (HTTP/1.1 allows it)
    const std::string_view codeStr = (sp2 == std::string_view::npos)
        ? statusLine.substr(sp1 + 1)
        : statusLine.substr(sp1 + 1, sp2 - sp1 - 1);

    // Parse status code
    int code = 0;
    for (char c : codeStr) {
        if (c < '0' || c > '9') {
            markError_();
            return false;
        }
        code = code * 10 + (c - '0');
    }
    if (code < 100 || code > 599) {
        markError_();
        return false;
    }

    const std::string_view versionTok = statusLine.substr(0, sp1);
    if (versionTok != "HTTP/1.0" && versionTok != "HTTP/1.1") {
        markError_();
        return false;
    }

    response_.statusCode = code;
    response_.version_ = std::string(versionTok);
    response_.statusText_ = (sp2 == std::string_view::npos)
        ? std::string{}
        : std::string(statusLine.substr(sp2 + 1));

    // Parse header fields
    if (firstNL != std::string_view::npos)
        detail::parseHeaderFields(
            hdr, firstNL, [this](std::string key, std::string_view val) {
                response_.headers_[std::move(key)] = std::string(val);
            });

    determineBodyMode_();
    if (state_ == State::Error) return false;
    headersParsed_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// determineBodyMode_
//
// Sets bodyMode_ (and contentLength_ for Content-Length responses) by
// inspecting Transfer-Encoding and Content-Length headers that are already
// in response_.headers_.
// Called once at the end of tryParseHeaders_().
// ---------------------------------------------------------------------------
void HttpResponseParser::determineBodyMode_() {
    const std::string* te = response_.header("transfer-encoding");
    if (te != nullptr) {
        // RFC 7230 §3.3.1: "chunked" is the last transfer-coding token.
        std::string_view tev = *te;
        const size_t lastComma = tev.rfind(',');
        std::string_view lastToken = (lastComma == std::string_view::npos)
            ? tev
            : tev.substr(lastComma + 1);
        const size_t ts = lastToken.find_first_not_of(" \t");
        if (ts != std::string_view::npos) lastToken = lastToken.substr(ts);
        const size_t te2 = lastToken.find_last_not_of(" \t");
        if (te2 != std::string_view::npos)
            lastToken = lastToken.substr(0, te2 + 1);

        if (lastToken.size() == 7) {
            char low[7];
            for (size_t i = 0; i < 7; ++i)
                low[i] = static_cast<char>(
                    ::tolower(static_cast<unsigned char>(lastToken[i])));
            if (std::string_view(low, 7) == "chunked")
                bodyMode_ = BodyMode::Chunked;
        }
    }

    if (bodyMode_ == BodyMode::Unknown) {
        const std::string* cl = response_.header("content-length");
        if (cl != nullptr) {
            int64_t parsed = 0;
            auto [ptr, ec]
                = std::from_chars(cl->data(), cl->data() + cl->size(), parsed);
            if (ec == std::errc{} && ptr == cl->data() + cl->size()
                && parsed >= 0)
                contentLength_ = parsed;
            else {
                markError_();
                return;
            }
            if (contentLength_ > static_cast<int64_t>(kMaxBodyLen)) {
                markError_();
                return;
            }
            bodyMode_ = BodyMode::ContentLength;
        } else {
            bodyMode_ = BodyMode::ConnectionClose;
        }
    }
}

// ---------------------------------------------------------------------------
// processBody_ — for ContentLength and ConnectionClose modes
// ---------------------------------------------------------------------------
HttpResponseParser::State HttpResponseParser::processBody_() {
    if (bodyMode_ == BodyMode::ContentLength) {
        if (contentLength_ == 0) {
            response_.body_.clear();
            markComplete_();
            return state_;
        }
        if (static_cast<int64_t>(bodyBuf_.size()) >= contentLength_) {
            response_.body_.assign(
                bodyBuf_.data(), static_cast<size_t>(contentLength_));
            markComplete_();
        }
    }
    // ConnectionClose: body grows until feedEof() — nothing to do here
    return state_;
}

// ---------------------------------------------------------------------------
// processChunked_
//
// Pulls complete chunks out of bodyBuf_, appending decoded data to
// decodedBody_.  On the terminal "0\r\n\r\n" chunk, calls markComplete_().
//
// Chunk format (RFC 7230 §4.1):
//   chunk-size (hex) [ chunk-ext ] CRLF  chunk-data CRLF
//   ...
//   "0" CRLF [ trailers ] CRLF
// ---------------------------------------------------------------------------
HttpResponseParser::State HttpResponseParser::processChunked_() {
    // We scan from chunkScanPos_ through bodyBuf_.
    // When we successfully consume a chunk we update chunkScanPos_ so future
    // calls don't re-scan from zero.

    while (true) {
        // Compact consumed prefix to avoid unbounded memory growth
        if (chunkScanPos_ > 4096 && chunkScanPos_ > bodyBuf_.size() / 2) {
            bodyBuf_.erase(0, chunkScanPos_);
            chunkScanPos_ = 0;
        }

        // Need at least "0\r\n\r\n" (5 bytes) to be able to terminate
        if (chunkScanPos_ >= bodyBuf_.size()) break;

        // Find the end of the chunk-size line
        const size_t crlfPos = bodyBuf_.find("\r\n", chunkScanPos_);
        if (crlfPos == std::string::npos) break; // need more data

        // Parse hex chunk size (chunk-extensions stripped inside
        // parseChunkSize_)
        const std::string_view sizeLine(
            bodyBuf_.data() + chunkScanPos_, crlfPos - chunkScanPos_);
        size_t chunkSize = 0;
        if (!parseChunkSize_(sizeLine, chunkSize)) {
            markError_();
            return state_;
        }

        if (chunkSize == 0) {
            // Terminal chunk — skip "0\r\n" + optional trailers + "\r\n\r\n"
            // crlfPos is the position of the "\r\n" after "0".
            // The trailer section ends at the next blank line ("\r\n\r\n").
            // For no-trailer case: "0\r\n\r\n" — the \r\n at crlfPos IS the
            // start of the \r\n\r\n terminator, so search from crlfPos.
            const size_t trailerEnd = bodyBuf_.find("\r\n\r\n", crlfPos);
            if (trailerEnd == std::string::npos) break; // need more data
            chunkScanPos_ = trailerEnd + 4;
            // Terminal chunk consumed — body complete
            response_.body_ = std::move(decodedBody_);
            markComplete_();
            return state_;
        }

        // Need: crlfPos + 2 + chunkSize + 2 (trailing \r\n)
        const size_t dataStart = crlfPos + 2;
        const size_t dataEnd = dataStart + chunkSize;
        const size_t nextChunk = dataEnd + 2; // skip trailing \r\n

        if (nextChunk > bodyBuf_.size()) break; // need more data

        // Validate trailing CRLF
        if (bodyBuf_[dataEnd] != '\r' || bodyBuf_[dataEnd + 1] != '\n') {
            markError_();
            return state_;
        }

        if (decodedBody_.size() > kMaxBodyLen - chunkSize) {
            markError_();
            return state_;
        }
        decodedBody_.append(bodyBuf_.data() + dataStart, chunkSize);
        chunkScanPos_ = nextChunk;
    }

    return state_;
}

// ---------------------------------------------------------------------------
// advanceAfterHeaders_ — called once headers are parsed; routes body handling
// ---------------------------------------------------------------------------
HttpResponseParser::State HttpResponseParser::advanceAfterHeaders_() {
    state_ = State::HeadersComplete;

    // 1xx, 204, 304 have no body regardless of headers
    const int code = response_.statusCode;
    if (code == 204 || code == 304 || (code >= 100 && code < 200)) {
        markComplete_();
        return state_;
    }

    if (bodyMode_ == BodyMode::Chunked) return processChunked_();

    return processBody_();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void HttpResponseParser::reset() {
    inBuf_.clear();
    headerBuf_.clear();
    bodyBuf_.clear();
    decodedBody_.clear();

    // Reserve space to prevent reallocation during parsing
    inBuf_.reserve(16384);
    headerBuf_.reserve(16384);
    bodyBuf_.reserve(16384);
    decodedBody_.reserve(16384);

    state_ = State::Incomplete;
    headersParsed_ = false;
    headerScanPos_ = 0;
    bodyMode_ = BodyMode::Unknown;
    contentLength_ = -1;
    chunkScanPos_ = 0;
    response_ = HttpResponse{};
}

HttpResponseParser::State HttpResponseParser::feed(
    const char* data, size_t len) {
    if (state_ == State::Complete || state_ == State::Error) return state_;

    if (data == nullptr || len == 0) return state_;

    if (!headersParsed_) {
        inBuf_.append(data, len);
        if (!tryParseHeaders_()) {
            // Either need more data (state_ == Incomplete)
            // or malformed (state_ == Error, set by tryParseHeaders_)
            if (state_ != State::Error && inBuf_.size() > kMaxHeaderSectionLen)
                markError_();
            return state_;
        }
        return advanceAfterHeaders_();
    }

    // Headers already parsed — accumulate further body bytes
    if (len > kMaxBodyLen || bodyBuf_.size() > kMaxBodyLen - len) {
        markError_();
        return state_;
    }
    bodyBuf_.append(data, len);

    if (bodyMode_ == BodyMode::Chunked) return processChunked_();
    return processBody_();
}

HttpResponseParser::State HttpResponseParser::feedEof() {
    if (state_ == State::Complete || state_ == State::Error) return state_;

    if (!headersParsed_) {
        // Got EOF before headers — attempt to parse what we have as a 0-body
        // response, or treat as error
        markError_();
        return state_;
    }

    if (bodyMode_ == BodyMode::ConnectionClose) {
        response_.body_ = std::move(bodyBuf_);
        markComplete_();
    } else if (bodyMode_ == BodyMode::Chunked) {
        // Truncated chunked response
        markError_();
    } else {
        // ContentLength not satisfied
        markError_();
    }
    return state_;
}

// ---------------------------------------------------------------------------
// HttpResponse::Builder
// ---------------------------------------------------------------------------

std::string_view HttpResponse::Builder::getReason_(int code) const {
    if (!reason_.empty()) return reason_;
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

std::string HttpResponse::Builder::build() const {
    std::string r;
    r.reserve(512 + body_.size());

    r += "HTTP/1.1 ";
    r += std::to_string(code_);
    r += " ";
    r += getReason_(code_);
    r += "\r\n";

    bool hasContentLength = false;
    for (const auto& h : headers_) {
        r += h.first;
        r += ": ";
        r += h.second;
        r += "\r\n";
        // Case-insensitive comparison so "content-length" also suppresses
        // duplicate
        const auto& k = h.first;
        if (k.size() == 14) {
            std::string kl(k.size(), '\0');
            for (size_t i = 0; i < k.size(); ++i)
                kl[i] = static_cast<char>(
                    ::tolower(static_cast<unsigned char>(k[i])));
            if (kl == "content-length") hasContentLength = true;
        }
    }

    if (!hasContentLength) {
        r += "Content-Length: ";
        r += std::to_string(body_.size());
        r += "\r\n";
    }

    r += "Connection: ";
    r += keepAlive_ ? "keep-alive" : "close";
    r += "\r\n\r\n";
    r += body_;

    return r;
}

} // namespace aiSocks
