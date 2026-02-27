// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
//
// Tests for HttpPollServer::urlEncode / urlDecode.
//
// Coverage goals
// --------------
// 1.  Empty string edge case
// 2.  Unreserved characters pass through unchanged (RFC 3986 S.2.3)
// 3.  All ASCII characters that MUST be encoded produce %XX
// 4.  Uppercase hex digits in encoder output
// 5.  Decoder handles both uppercase and lowercase hex
// 6.  '+' in decoder -> space (form-encoding convention)
// 7.  '%2B' in decoder -> '+' (not space)
// 8.  Truncated/invalid %XX sequences pass through verbatim
//     (WHATWG: %25%s%1G -> %%s%1G)
// 9.  '%' at end-of-string and '%X' (only one hex digit) -> literal
// 10. Null byte ('\0') encodes/decodes correctly
// 11. High bytes (0x80-0xFF) encode as %XX and round-trip
// 12. Double-percent / one-level-only decode (%2525 -> %25)
// 13. Full round-trips: urlDecode(urlEncode(s)) == s
// 14. '~' is treated as unreserved (historical curl/PHP bug target)
// 15. Space encodes to %20 (not +)
// 16. Mixed plain+encoded input round-trip

#include "UrlCodec.h"
#include "test_helpers.h"
#include <iostream>
#include <string>

using namespace aiSocks;

// -- helpers ----------------------------------------------------------------
static std::string enc(const std::string& s) {
    return aiSocks::urlEncode(s);
}
static std::string dec(const std::string& s) {
    return aiSocks::urlDecode(s);
}

// Check encode result and print it so failures are easy to diagnose.
static void CHECK_ENC(const std::string& input, const std::string& expected) {
    const std::string got = enc(input);
    REQUIRE_MSG(got == expected,
        "urlEncode(\"" + input + "\") == \"" + expected + "\"  (got \"" + got
            + "\")");
}
static void CHECK_DEC(const std::string& input, const std::string& expected) {
    const std::string got = dec(input);
    REQUIRE_MSG(got == expected,
        "urlDecode(\"" + input + "\") == \"" + expected + "\"  (got \"" + got
            + "\")");
}
static void CHECK_ROUNDTRIP(const std::string& original) {
    const std::string got = dec(enc(original));
    REQUIRE_MSG(got == original,
        "round-trip: decode(encode(s)) == s  for s=\"" + original + "\"");
}

// -- test functions ---------------------------------------------------------

static void test_empty() {
    BEGIN_TEST("empty string");
    CHECK_ENC("", "");
    CHECK_DEC("", "");
}

static void test_unreserved_chars() {
    BEGIN_TEST("unreserved characters pass through (RFC 3986 S.2.3)");
    // Letters
    CHECK_ENC("abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz");
    CHECK_ENC("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    // Digits
    CHECK_ENC("0123456789", "0123456789");
    // The four unreserved symbols
    CHECK_ENC("-", "-");
    CHECK_ENC("_", "_");
    CHECK_ENC(".", ".");
    CHECK_ENC("~", "~"); // historical bug target -- must NOT be encoded

    // All at once
    CHECK_ENC("Hello-World_1.0~", "Hello-World_1.0~");
}

static void test_space_and_common() {
    BEGIN_TEST("space and common special characters");
    // Space -> %20 (encoder never uses '+')
    CHECK_ENC(" ", "%20");
    CHECK_ENC("hello world", "hello%20world");

    // Characters that must be encoded
    CHECK_ENC("!", "%21");
    CHECK_ENC("#", "%23");
    CHECK_ENC("$", "%24");
    CHECK_ENC("%", "%25"); // percent itself
    CHECK_ENC("&", "%26");
    CHECK_ENC("+", "%2B");
    CHECK_ENC(",", "%2C");
    CHECK_ENC("/", "%2F");
    CHECK_ENC(":", "%3A");
    CHECK_ENC(";", "%3B");
    CHECK_ENC("=", "%3D");
    CHECK_ENC("?", "%3F");
    CHECK_ENC("@", "%40");
    CHECK_ENC("[", "%5B");
    CHECK_ENC("]", "%5D");
    CHECK_ENC("|", "%7C");
}

static void test_uppercase_hex_output() {
    BEGIN_TEST("encoder always outputs uppercase hex digits");
    // 0x0a -> must be %0A, not %0a
    std::string s(1, '\x0a');
    const std::string got = enc(s);
    REQUIRE_MSG(
        got == "%0A", "urlEncode(\"\\x0a\") == \"%0A\"  (got \"" + got + "\")");

    // 0xfe -> must be %FE
    s = std::string(1, '\xfe');
    const std::string got2 = enc(s);
    REQUIRE_MSG(got2 == "%FE",
        "urlEncode(\"\\xfe\") == \"%FE\"  (got \"" + got2 + "\")");
}

static void test_decoder_case_insensitive() {
    BEGIN_TEST("decoder accepts both uppercase and lowercase hex");
    CHECK_DEC("%2f", "/");
    CHECK_DEC("%2F", "/");
    CHECK_DEC("%2e", ".");
    CHECK_DEC("%2E", ".");
    CHECK_DEC("%61", "a"); // 'a' -- lower hex
    CHECK_DEC("%61%62%63", "abc");
}

static void test_plus_decoding() {
    BEGIN_TEST("'+' in decoder -> space (form-encoding convention)");
    CHECK_DEC("+", " ");
    CHECK_DEC("hello+world", "hello world");
    CHECK_DEC("a+b+c", "a b c");
    // But '%2B' must decode to literal '+', not space
    CHECK_DEC("%2B", "+");
    CHECK_DEC("a%2Bb", "a+b");
}

static void test_invalid_percent_sequences() {
    BEGIN_TEST("invalid / truncated %XX -> pass through verbatim");
    // WHATWG spec example: %25%s%1G -> %%s%1G
    CHECK_DEC("%25%s%1G", "%%s%1G");

    // '%' at very end of string -- no following chars
    CHECK_DEC("abc%", "abc%");

    // Only one hex digit after '%'
    CHECK_DEC("abc%4", "abc%4");
    CHECK_DEC("%4", "%4");

    // Non-hex digits
    CHECK_DEC("%GG", "%GG");
    CHECK_DEC("%ZZ", "%ZZ");
    CHECK_DEC("%1G", "%1G"); // second digit invalid
    CHECK_DEC("%G1", "%G1"); // first digit invalid

    // Valid sandwiched between invalid
    CHECK_DEC("%GG%20%ZZ", "%GG %ZZ");
}

static void test_null_byte() {
    BEGIN_TEST("null byte (\\x00) encodes and decodes correctly");
    std::string nul(1, '\0');
    CHECK_ENC(nul, "%00");
    CHECK_DEC("%00", nul);
    // Round-trip through a larger string
    std::string mixed = "a\x00z";
    mixed[1] = '\0'; // suppress string-literal truncation warnings
    CHECK_ROUNDTRIP(mixed);
}

static void test_high_bytes() {
    BEGIN_TEST("high bytes (0x80-0xFF) round-trip correctly");
    for (int c = 0x80; c <= 0xFF; ++c) {
        std::string s(1, static_cast<char>(c));
        const std::string encoded = enc(s);
        // Must start with '%' and be 3 chars
        REQUIRE_MSG(encoded.size() == 3 && encoded[0] == '%',
            "high byte 0x" + std::to_string(c) + " encodes as 3-char %XX");
        // Must round-trip
        REQUIRE_MSG(dec(encoded) == s,
            "high byte 0x" + std::to_string(c) + " round-trips");
    }
}

static void test_double_percent_one_level_only() {
    BEGIN_TEST("decoder is single-pass: does not double-decode");
    // %2525 is '%' encoded then '25'; one pass gives %25, not '%'
    CHECK_DEC("%2525", "%25");
    // %252F -> %2F  (not '/')
    CHECK_DEC("%252F", "%2F");
}

static void test_round_trips() {
    BEGIN_TEST("round-trip: decode(encode(s)) == s");
    CHECK_ROUNDTRIP("Hello, World!");
    CHECK_ROUNDTRIP("foo bar baz");
    CHECK_ROUNDTRIP("a+b=c&d=e");
    CHECK_ROUNDTRIP("https://example.com/path?q=1#frag");
    CHECK_ROUNDTRIP("~unreserved-chars_are.fine");
    CHECK_ROUNDTRIP("100% done!");
    CHECK_ROUNDTRIP("??+??"); // UTF-8 multi-byte
    CHECK_ROUNDTRIP("\x01\x7F\x80\xFF");
    CHECK_ROUNDTRIP("a/b/c?x=1&y=2");
    CHECK_ROUNDTRIP("");

    // Complex query string
    CHECK_ROUNDTRIP("name=John Doe&email=john@example.com&score=100%");
}

static void test_tilde_not_encoded() {
    BEGIN_TEST("'~' is unreserved and must NOT be percent-encoded");
    // Historical bug: curl < 7.77, PHP rawurlencode() used to encode '~'
    CHECK_ENC("~", "~");
    CHECK_ENC("~test~", "~test~");
    // But it decodes correctly if someone sent %7E
    CHECK_DEC("%7E", "~");
    CHECK_DEC("%7e", "~");
}

static void test_mixed_plain_and_encoded() {
    BEGIN_TEST("mixed plain text and percent-encoded sequences");
    // Partially encoded string from a browser
    CHECK_DEC("hello%20world%21", "hello world!");
    CHECK_DEC("foo%3Dbar%26baz%3Dqux", "foo=bar&baz=qux");
    // Encoder on a string that already contains '%' -> encodes the '%'
    CHECK_ENC("50% off", "50%25%20off");
    // Decode it back
    CHECK_DEC("50%25%20off", "50% off");
}

static void test_all_bytes_encode_decode() {
    BEGIN_TEST("every byte value (0x00-0xFF) round-trips");
    for (int c = 0; c <= 0xFF; ++c) {
        std::string s(1, static_cast<char>(c));
        REQUIRE_MSG(
            dec(enc(s)) == s, "encode-decode round-trip for byte 0x" + [&] {
                std::string h;
                h += "0123456789ABCDEF"[c >> 4];
                h += "0123456789ABCDEF"[c & 15];
                return h;
            }());
    }
}

// -- main -------------------------------------------------------------------
int main() {
    std::cout << "=== url_codec tests ===\n";

    test_empty();
    test_unreserved_chars();
    test_space_and_common();
    test_uppercase_hex_output();
    test_decoder_case_insensitive();
    test_plus_decoding();
    test_invalid_percent_sequences();
    test_null_byte();
    test_high_bytes();
    test_double_percent_one_level_only();
    test_round_trips();
    test_tilde_not_encoded();
    test_mixed_plain_and_encoded();
    test_all_bytes_encode_decode();

    return test_summary();
}
