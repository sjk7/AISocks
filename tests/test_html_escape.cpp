// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests for the HtmlEscape utility (lib/include/HtmlEscape.h).

#include "HtmlEscape.h"
#include "test_helpers.h"

using namespace aiSocks;

// ============================================================
// encode — character-level substitutions
// ============================================================

static void test_encode_empty() {
    BEGIN_TEST("HtmlEscape::encode: empty string -> empty string");
    REQUIRE(HtmlEscape::encode("") == "");
}

static void test_encode_no_special_chars() {
    BEGIN_TEST("HtmlEscape::encode: plain ASCII is returned unchanged");
    REQUIRE(HtmlEscape::encode("Hello, World!") == "Hello, World!");
}

static void test_encode_ampersand() {
    BEGIN_TEST("HtmlEscape::encode: '&' -> '&amp;'");
    REQUIRE(HtmlEscape::encode("a&b") == "a&amp;b");
}

static void test_encode_less_than() {
    BEGIN_TEST("HtmlEscape::encode: '<' -> '&lt;'");
    REQUIRE(HtmlEscape::encode("a<b") == "a&lt;b");
}

static void test_encode_greater_than() {
    BEGIN_TEST("HtmlEscape::encode: '>' -> '&gt;'");
    REQUIRE(HtmlEscape::encode("a>b") == "a&gt;b");
}

static void test_encode_double_quote() {
    BEGIN_TEST("HtmlEscape::encode: '\"' -> '&quot;'");
    REQUIRE(HtmlEscape::encode("say \"hi\"") == "say &quot;hi&quot;");
}

static void test_encode_single_quote() {
    BEGIN_TEST("HtmlEscape::encode: '\\'' -> '&#39;'");
    REQUIRE(HtmlEscape::encode("it's") == "it&#39;s");
}

// ============================================================
// encode — combinations and real-world strings
// ============================================================

static void test_encode_all_special_chars() {
    BEGIN_TEST("HtmlEscape::encode: all five special chars in one string");
    REQUIRE(HtmlEscape::encode("&<>\"'") == "&amp;&lt;&gt;&quot;&#39;");
}

static void test_encode_xss_script_tag() {
    BEGIN_TEST("HtmlEscape::encode: typical reflected-XSS payload is neutralised");
    std::string input  = "<script>alert('xss')</script>";
    std::string expect = "&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;";
    REQUIRE(HtmlEscape::encode(input) == expect);
}

static void test_encode_xss_attribute_injection() {
    BEGIN_TEST("HtmlEscape::encode: attribute-injection payload is neutralised");
    std::string input  = "\" onload=\"alert(1)";
    std::string expect = "&quot; onload=&quot;alert(1)";
    REQUIRE(HtmlEscape::encode(input) == expect);
}

static void test_encode_multiple_special_chars_scattered() {
    BEGIN_TEST("HtmlEscape::encode: special chars scattered among plain text");
    REQUIRE(HtmlEscape::encode("a&b<c>d\"e'f") ==
            "a&amp;b&lt;c&gt;d&quot;e&#39;f");
}

static void test_encode_only_special_chars() {
    BEGIN_TEST("HtmlEscape::encode: input consisting entirely of special chars");
    REQUIRE(HtmlEscape::encode("<<<") == "&lt;&lt;&lt;");
    REQUIRE(HtmlEscape::encode(">>>") == "&gt;&gt;&gt;");
    REQUIRE(HtmlEscape::encode("&&&") == "&amp;&amp;&amp;");
}

static void test_encode_does_not_double_encode() {
    BEGIN_TEST("HtmlEscape::encode: already-escaped entities are re-escaped (no double-encode)");
    // The function escapes '&' unconditionally, so "&amp;" becomes "&amp;amp;".
    // This is the correct, safe behaviour for a raw-input encoder.
    REQUIRE(HtmlEscape::encode("&amp;") == "&amp;amp;");
}

static void test_encode_preserves_unicode_bytes() {
    BEGIN_TEST("HtmlEscape::encode: non-ASCII bytes are passed through unchanged");
    // UTF-8 for U+00E9 (é): 0xC3 0xA9
    std::string input  = "\xC3\xA9";
    std::string result = HtmlEscape::encode(input);
    REQUIRE(result == input);
}

static void test_encode_long_plain_string() {
    BEGIN_TEST("HtmlEscape::encode: long plain string has same content after encode");
    std::string plain(1000, 'a');
    REQUIRE(HtmlEscape::encode(plain) == plain);
}

// ============================================================
// main
// ============================================================

int main() {
    // Character-level substitutions
    test_encode_empty();
    test_encode_no_special_chars();
    test_encode_ampersand();
    test_encode_less_than();
    test_encode_greater_than();
    test_encode_double_quote();
    test_encode_single_quote();

    // Combinations / real-world strings
    test_encode_all_special_chars();
    test_encode_xss_script_tag();
    test_encode_xss_attribute_injection();
    test_encode_multiple_special_chars_scattered();
    test_encode_only_special_chars();
    test_encode_does_not_double_encode();
    test_encode_preserves_unicode_bytes();
    test_encode_long_plain_string();

    return test_summary();
}
