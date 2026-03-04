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

#include <string>

namespace aiSocks {

// Percent-encode src.  Unreserved chars (A-Z a-z 0-9 - _ . ~) pass through.
std::string urlEncode(const std::string& src);

// Decode a percent-encoded string.  Invalid sequences are passed verbatim.
std::string urlDecode(const std::string& src);

} // namespace aiSocks

#endif // AISOCKS_URL_CODEC_H
