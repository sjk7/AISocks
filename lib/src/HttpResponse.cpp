// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpResponse.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>

namespace aiSocks {

// ---------------------------------------------------------------------------
// HttpResponse
// ---------------------------------------------------------------------------

const std::string_view* HttpResponse::header(std::string_view name) const {
    // Lowercase the lookup key without allocating when it fits in a small buffer
    char smallBuf[128];
    std::string heapBuf;
    const char* keyData = nullptr;

    if (name.size() < sizeof(smallBuf)) {
        for (size_t i = 0; i < name.size(); ++i)
            smallBuf[i] = static_cast<char>(
                ::tolower(static_cast<unsigned char>(name[i])));
        smallBuf[name.size()] = '\0';
        keyData = smallBuf;
    } else {
        heapBuf.resize(name.size());
        for (size_t i = 0; i < name.size(); ++i)
            heapBuf[i] = static_cast<char>(
                ::tolower(static_cast<unsigned char>(name[i])));
        keyData = heapBuf.c_str();
    }

    auto it = headers_.find(std::string(keyData, name.size()));
    return it == headers_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// HttpResponseParser — helpers
// ---------------------------------------------------------------------------

void HttpResponseParser::markError_() {
    state_ = State::Error;
}

void HttpResponseParser::markComplete_() {
    response_.valid = true;
    state_ = State::Complete;
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
    // Incremental scan for \r\n\r\n  ------------------------------------- //
    const size_t start = headerScanPos_ >= 3 ? headerScanPos_ - 3 : 0;
    const size_t sepPos = inBuf_.find("\r\n\r\n", start);
    if (sepPos == std::string::npos) {
        // Not yet — advance scan frontier (keep 3 tail bytes for overlap)
        headerScanPos_ = inBuf_.size() >= 3 ? inBuf_.size() - 3 : 0;
        return false; // really means "not ready yet" — caller checks bool
    }

    // Freeze header section
    headerBuf_ = inBuf_.substr(0, sepPos);
    // Any bytes after \r\n\r\n belong to the body
    if (sepPos + 4 < inBuf_.size())
        bodyBuf_ = inBuf_.substr(sepPos + 4);
    inBuf_.clear();
    inBuf_.shrink_to_fit();

    const std::string_view hdr(headerBuf_);

    // Parse status line: HTTP-version SP status-code SP reason-phrase CRLF
    const size_t firstCRLF = hdr.find("\r\n");
    const std::string_view statusLine
        = (firstCRLF == std::string_view::npos) ? hdr : hdr.substr(0, firstCRLF);

    const size_t sp1 = statusLine.find(' ');
    if (sp1 == std::string_view::npos) { markError_(); return false; }

    const size_t sp2 = statusLine.find(' ', sp1 + 1);
    // reason-phrase may be absent (HTTP/1.1 allows it)
    const std::string_view codeStr = (sp2 == std::string_view::npos)
        ? statusLine.substr(sp1 + 1)
        : statusLine.substr(sp1 + 1, sp2 - sp1 - 1);

    // Parse status code
    int code = 0;
    for (char c : codeStr) {
        if (c < '0' || c > '9') { markError_(); return false; }
        code = code * 10 + (c - '0');
    }
    if (code < 100 || code > 599) { markError_(); return false; }

    response_.statusCode = code;
    response_.version_   = hdr.substr(0, sp1);   // view into frozen headerBuf_
    response_.statusText_ = (sp2 == std::string_view::npos)
        ? std::string_view{}
        : hdr.substr(sp2 + 1, firstCRLF != std::string_view::npos
                                   ? firstCRLF - (sp2 + 1)
                                   : std::string_view::npos);

    // Parse header fields
    if (firstCRLF != std::string_view::npos) {
        size_t pos = firstCRLF + 2;
        while (pos < hdr.size()) {
            const size_t lineEnd = hdr.find("\r\n", pos);
            const std::string_view line = (lineEnd == std::string_view::npos)
                ? hdr.substr(pos)
                : hdr.substr(pos, lineEnd - pos);

            if (line.empty()) break;

            const size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                std::string_view rawKey = line.substr(0, colon);
                std::string_view rawVal = line.substr(colon + 1);

                // lowercase key — must be a std::string for map key
                std::string key;
                key.resize(rawKey.size());
                for (size_t i = 0; i < rawKey.size(); ++i)
                    key[i] = static_cast<char>(
                        ::tolower(static_cast<unsigned char>(rawKey[i])));

                // trim leading/trailing whitespace from value (view only)
                const size_t valStart
                    = rawVal.find_first_not_of(" \t");
                if (valStart != std::string_view::npos) {
                    rawVal = rawVal.substr(valStart);
                    const size_t valEnd = rawVal.find_last_not_of(" \t\r");
                    if (valEnd != std::string_view::npos)
                        rawVal = rawVal.substr(0, valEnd + 1);
                    else
                        rawVal = {};
                } else {
                    rawVal = {};
                }

                // last value wins for duplicate header names
                response_.headers_[std::move(key)] = rawVal;
            }

            if (lineEnd == std::string_view::npos) break;
            pos = lineEnd + 2;
        }
    }

    // Determine body mode from headers
    const std::string_view* te = response_.header("transfer-encoding");
    if (te != nullptr) {
        // Check for "chunked" as last token (RFC 7230 §3.3.1)
        std::string_view tev = *te;
        // Find last comma-separated token
        const size_t lastComma = tev.rfind(',');
        std::string_view lastToken
            = (lastComma == std::string_view::npos)
                ? tev
                : tev.substr(lastComma + 1);
        // trim
        const size_t ts = lastToken.find_first_not_of(" \t");
        if (ts != std::string_view::npos) lastToken = lastToken.substr(ts);
        const size_t te2 = lastToken.find_last_not_of(" \t");
        if (te2 != std::string_view::npos)
            lastToken = lastToken.substr(0, te2 + 1);

        // Case-insensitive compare
        if (lastToken.size() == 7) {
            char low[7];
            for (int i = 0; i < 7; ++i)
                low[i] = static_cast<char>(
                    ::tolower(static_cast<unsigned char>(lastToken[i])));
            if (std::string_view(low, 7) == "chunked")
                bodyMode_ = BodyMode::Chunked;
        }
    }

    if (bodyMode_ == BodyMode::Unknown) {
        const std::string_view* cl = response_.header("content-length");
        if (cl != nullptr) {
            contentLength_ = static_cast<int64_t>(std::atoll(cl->data()));
            bodyMode_ = BodyMode::ContentLength;
        } else {
            bodyMode_ = BodyMode::ConnectionClose;
        }
    }

    headersParsed_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// processBody_ — for ContentLength and ConnectionClose modes
// ---------------------------------------------------------------------------
HttpResponseParser::State HttpResponseParser::processBody_() {
    if (bodyMode_ == BodyMode::ContentLength) {
        if (contentLength_ == 0) {
            response_.body_ = {};
            markComplete_();
            return state_;
        }
        if (static_cast<int64_t>(bodyBuf_.size()) >= contentLength_) {
            response_.body_ = std::string_view(
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
        // Need at least "0\r\n\r\n" (5 bytes) to be able to terminate
        if (chunkScanPos_ >= bodyBuf_.size()) break;

        // Find the end of the chunk-size line
        const size_t crlfPos = bodyBuf_.find("\r\n", chunkScanPos_);
        if (crlfPos == std::string::npos) break; // need more data

        // Parse hex chunk size
        const std::string_view sizeLine(
            bodyBuf_.data() + chunkScanPos_, crlfPos - chunkScanPos_);

        // Strip any chunk-extensions (after ';')
        const size_t extPos = sizeLine.find(';');
        const std::string_view hexStr
            = (extPos == std::string_view::npos)
                ? sizeLine
                : sizeLine.substr(0, extPos);

        if (hexStr.empty()) { markError_(); return state_; }

        // Parse hex
        size_t chunkSize = 0;
        for (char c : hexStr) {
            unsigned char uc = static_cast<unsigned char>(c);
            int digit = 0;
            if (uc >= '0' && uc <= '9')      digit = uc - '0';
            else if (uc >= 'a' && uc <= 'f') digit = uc - 'a' + 10;
            else if (uc >= 'A' && uc <= 'F') digit = uc - 'A' + 10;
            else { markError_(); return state_; }
            chunkSize = chunkSize * 16 + static_cast<size_t>(digit);
        }

        if (chunkSize == 0) {
            // Terminal chunk — skip "0\r\n" + optional trailers + "\r\n"
            // Find the final "\r\n" that closes the trailer section
            const size_t trailerEnd
                = bodyBuf_.find("\r\n", crlfPos + 2);
            if (trailerEnd == std::string::npos) break; // need more data
            // Terminal chunk consumed — body complete
            response_.body_ = std::string_view(decodedBody_);
            markComplete_();
            return state_;
        }

        // Need: crlfPos + 2 + chunkSize + 2 (trailing \r\n)
        const size_t dataStart = crlfPos + 2;
        const size_t dataEnd   = dataStart + chunkSize;
        const size_t nextChunk = dataEnd + 2; // skip trailing \r\n

        if (nextChunk > bodyBuf_.size()) break; // need more data

        // Validate trailing CRLF
        if (bodyBuf_[dataEnd] != '\r' || bodyBuf_[dataEnd + 1] != '\n') {
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

    if (bodyMode_ == BodyMode::Chunked)
        return processChunked_();

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
    state_         = State::Incomplete;
    headersParsed_ = false;
    headerScanPos_ = 0;
    bodyMode_      = BodyMode::Unknown;
    contentLength_ = -1;
    chunkScanPos_  = 0;
    response_      = HttpResponse{};
}

HttpResponseParser::State HttpResponseParser::feed(const char* data,
    size_t len) {
    if (state_ == State::Complete || state_ == State::Error)
        return state_;

    if (data == nullptr || len == 0) return state_;

    if (!headersParsed_) {
        inBuf_.append(data, len);
        if (!tryParseHeaders_()) {
            // Either need more data (state_ == Incomplete)
            // or malformed (state_ == Error, set by tryParseHeaders_)
            return state_;
        }
        return advanceAfterHeaders_();
    }

    // Headers already parsed — accumulate further body bytes
    bodyBuf_.append(data, len);

    if (bodyMode_ == BodyMode::Chunked)
        return processChunked_();
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
        response_.body_ = std::string_view(bodyBuf_);
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

} // namespace aiSocks
