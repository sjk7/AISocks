// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests for HtmlPageGenerator (lib/include/HtmlPageGenerator.h).
//
// The class is header-only and has no socket/server dependencies, so all
// tests are pure unit tests that run synchronously.

#include "HtmlPageGenerator.h"
#include "test_helpers.h"

#include <string>

using namespace aiSocks;

// Helper: check that a string contains a substring.
static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// ============================================================
// errorPage — structure
// ============================================================

static void test_error_page_contains_doctype() {
    BEGIN_TEST("HtmlPageGenerator::errorPage: output starts with <!DOCTYPE html>");
    HtmlPageGenerator gen;
    std::string html = gen.errorPage(404, "Not Found", "File not found");
    REQUIRE(html.substr(0, 15) == "<!DOCTYPE html>");
}

static void test_error_page_contains_code_in_title() {
    BEGIN_TEST("HtmlPageGenerator::errorPage: HTTP code appears in <title>");
    HtmlPageGenerator gen;
    std::string html = gen.errorPage(404, "Not Found", "File not found");
    REQUIRE(contains(html, "<title>404 Not Found</title>"));
}

static void test_error_page_contains_code_in_h1() {
    BEGIN_TEST("HtmlPageGenerator::errorPage: HTTP code appears in <h1>");
    HtmlPageGenerator gen;
    std::string html = gen.errorPage(404, "Not Found", "File not found");
    REQUIRE(contains(html, "<h1>404 Not Found</h1>"));
}

static void test_error_page_contains_message_in_p() {
    BEGIN_TEST("HtmlPageGenerator::errorPage: message appears in <p>");
    HtmlPageGenerator gen;
    std::string html = gen.errorPage(404, "Not Found", "File not found");
    REQUIRE(contains(html, "<p>File not found</p>"));
}

static void test_error_page_well_formed_close_tags() {
    BEGIN_TEST("HtmlPageGenerator::errorPage: closes </body></html>");
    HtmlPageGenerator gen;
    std::string html = gen.errorPage(500, "Internal Server Error", "Oops");
    REQUIRE(contains(html, "</body></html>"));
}

// ============================================================
// errorPage — HTML escaping (security: XSS via user-controlled input)
// ============================================================

static void test_error_page_escapes_message_ampersand() {
    BEGIN_TEST("HtmlPageGenerator::errorPage: '&' in message is escaped");
    HtmlPageGenerator gen;
    std::string html = gen.errorPage(400, "Bad Request", "a&b");
    REQUIRE(contains(html, "a&amp;b"));
    REQUIRE(!contains(html, "<p>a&b</p>"));
}

static void test_error_page_escapes_message_less_than() {
    BEGIN_TEST("HtmlPageGenerator::errorPage: '<' in message is escaped");
    HtmlPageGenerator gen;
    std::string html = gen.errorPage(400, "Bad Request", "<script>alert(1)</script>");
    REQUIRE(!contains(html, "<script>"));
    REQUIRE(contains(html, "&lt;script&gt;"));
}

static void test_error_page_escapes_status() {
    BEGIN_TEST("HtmlPageGenerator::errorPage: special chars in status are escaped");
    HtmlPageGenerator gen;
    std::string html = gen.errorPage(400, "Bad <Request>", "details");
    REQUIRE(!contains(html, "Bad <Request>"));
    REQUIRE(contains(html, "Bad &lt;Request&gt;"));
}

// ============================================================
// errorPage — hideServerVersion flag
// ============================================================

static void test_error_page_hides_footer_by_default() {
    BEGIN_TEST("HtmlPageGenerator::errorPage: footer hidden when hideServerVersion=true (default)");
    HtmlPageGenerator gen(true);
    std::string html = gen.errorPage(403, "Forbidden", "Denied");
    REQUIRE(!contains(html, "aiSocks HttpFileServer"));
}

static void test_error_page_shows_footer_when_not_hidden() {
    BEGIN_TEST("HtmlPageGenerator::errorPage: footer shown when hideServerVersion=false");
    HtmlPageGenerator gen(false);
    std::string html = gen.errorPage(403, "Forbidden", "Denied");
    REQUIRE(contains(html, "aiSocks HttpFileServer"));
}

// ============================================================
// errorPage — various status codes
// ============================================================

static void test_error_page_400() {
    BEGIN_TEST("HtmlPageGenerator::errorPage: 400 code rendered correctly");
    HtmlPageGenerator gen;
    std::string html = gen.errorPage(400, "Bad Request", "Invalid syntax");
    REQUIRE(contains(html, "400"));
    REQUIRE(contains(html, "Bad Request"));
}

static void test_error_page_500() {
    BEGIN_TEST("HtmlPageGenerator::errorPage: 500 code rendered correctly");
    HtmlPageGenerator gen;
    std::string html = gen.errorPage(500, "Internal Server Error", "Server fault");
    REQUIRE(contains(html, "500"));
    REQUIRE(contains(html, "Internal Server Error"));
}

// ============================================================
// directoryListing — structure
// ============================================================

static void test_dir_listing_contains_doctype() {
    BEGIN_TEST("HtmlPageGenerator::directoryListing: output starts with <!DOCTYPE html>");
    HtmlPageGenerator gen;
    // Pass a non-existent directory to exercise the "Error reading directory"
    // code path; we only care about structural correctness here.
    std::string html = gen.directoryListing("/nonexistent_path_that_does_not_exist");
    REQUIRE(html.substr(0, 15) == "<!DOCTYPE html>");
}

static void test_dir_listing_contains_ul() {
    BEGIN_TEST("HtmlPageGenerator::directoryListing: output contains <ul> and </ul>");
    HtmlPageGenerator gen;
    std::string html = gen.directoryListing("/nonexistent_path_that_does_not_exist");
    REQUIRE(contains(html, "<ul>"));
    REQUIRE(contains(html, "</ul>"));
}

static void test_dir_listing_empty_dir_message() {
    BEGIN_TEST("HtmlPageGenerator::directoryListing: unreadable dir shows error item");
    HtmlPageGenerator gen;
    std::string html = gen.directoryListing("/nonexistent_path_that_does_not_exist");
    REQUIRE(contains(html, "Error reading directory"));
}

static void test_dir_listing_well_formed_close_tags() {
    BEGIN_TEST("HtmlPageGenerator::directoryListing: closes </body></html>");
    HtmlPageGenerator gen;
    std::string html = gen.directoryListing("/nonexistent_path_that_does_not_exist");
    REQUIRE(contains(html, "</body></html>"));
}

// ============================================================
// directoryListing — hideServerVersion flag
// ============================================================

static void test_dir_listing_hides_footer_by_default() {
    BEGIN_TEST("HtmlPageGenerator::directoryListing: footer hidden when hideServerVersion=true");
    HtmlPageGenerator gen(true);
    std::string html = gen.directoryListing("/nonexistent_path_that_does_not_exist");
    REQUIRE(!contains(html, "aiSocks HttpFileServer"));
}

static void test_dir_listing_shows_footer_when_not_hidden() {
    BEGIN_TEST("HtmlPageGenerator::directoryListing: footer shown when hideServerVersion=false");
    HtmlPageGenerator gen(false);
    std::string html = gen.directoryListing("/nonexistent_path_that_does_not_exist");
    REQUIRE(contains(html, "aiSocks HttpFileServer"));
}

// ============================================================
// main
// ============================================================

int main() {
    // errorPage — structure
    test_error_page_contains_doctype();
    test_error_page_contains_code_in_title();
    test_error_page_contains_code_in_h1();
    test_error_page_contains_message_in_p();
    test_error_page_well_formed_close_tags();

    // errorPage — HTML escaping
    test_error_page_escapes_message_ampersand();
    test_error_page_escapes_message_less_than();
    test_error_page_escapes_status();

    // errorPage — hideServerVersion
    test_error_page_hides_footer_by_default();
    test_error_page_shows_footer_when_not_hidden();

    // errorPage — status codes
    test_error_page_400();
    test_error_page_500();

    // directoryListing — structure
    test_dir_listing_contains_doctype();
    test_dir_listing_contains_ul();
    test_dir_listing_empty_dir_message();
    test_dir_listing_well_formed_close_tags();

    // directoryListing — hideServerVersion
    test_dir_listing_hides_footer_by_default();
    test_dir_listing_shows_footer_when_not_hidden();

    return test_summary();
}
