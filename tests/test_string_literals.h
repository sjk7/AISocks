#pragma once
// String literals for test_custom_file_server.cpp
// Separated to avoid auto-formatting corruption issues

namespace TestStringLiterals {

// Error HTML generation strings
inline const char* ERROR_HTML_BODY_STYLE
    = "body { font-family: Arial, sans-serif; margin: 40px; background: "
      "#f5f5f5; }";
inline const char* ERROR_HTML_CONTAINER_STYLE
    = ".error-container { max-width: 600px; margin: 0 auto; background: white; "
      "padding: 40px; border-radius: 8px; box-shadow: 0 4px 6px "
      "rgba(0,0,0,0.1); }";
inline const char* ERROR_HTML_H1_STYLE
    = "h1 { color: #e74c3c; margin-bottom: 20px; }";
inline const char* ERROR_HTML_P_STYLE = "p { color: #555; line-height: 1.6; }";
inline const char* ERROR_HTML_LINK_STYLE
    = ".back-link { display: inline-block; margin-top: 20px; padding: 10px "
      "20px; background: #3498db; color: white; text-decoration: none; "
      "border-radius: 4px; }";
inline const char* ERROR_HTML_LINK_HOVER_STYLE
    = ".back-link:hover { background: #2980b9; }";

// Auth required message
inline const char* AUTH_REQUIRED_MESSAGE
    = "This server requires authentication. Please provide valid credentials.";

// Testing instructions strings
inline const char* TESTING_INSTRUCTIONS_TITLE
    = "<!DOCTYPE html>\n<html><head><title>HttpFileServer - Testing "
      "Guide</title>";
inline const char* TESTING_INSTRUCTIONS_STYLE
    = "<style>body { font-family: Arial, sans-serif; margin: 20px; "
      "}</style></head><body>";
inline const char* TESTING_INSTRUCTIONS_HEADER
    = "<h1>🚀 HttpFileServer Testing Guide</h1>";
inline const char* TESTING_INSTRUCTIONS_INTRO
    = "<p>This is the testing interface for the CustomFileServer.</p>";
inline const char* TESTING_INSTRUCTIONS_AUTH
    = "<h2>🔐 Authentication</h2><p>Username: admin, Password: secret</p>";
inline const char* TESTING_INSTRUCTIONS_FILES
    = "<h2>📄 Test Files</h2><p><a href=\"/index.html\">index.html</a>"
      " | <a href=\"/style.css\">style.css</a>"
      " | <a href=\"/script.js\">script.js</a></p>";
inline const char* TESTING_INSTRUCTIONS_DIR
    = "<h2>📁 Directory Listing</h2><p><a href=\"/subdir/\">subdir/</a></p>";
inline const char* TESTING_INSTRUCTIONS_ACCESS
    = "<h2>🚫 Access Control</h2><p><a href=\"/config.conf\">config"
      ".conf</a> (should be blocked)</p>";
inline const char* TESTING_INSTRUCTIONS_ERROR
    = "<h2>❌ Error Testing</h2><p><a href=\"/nonexistent.html\">404 "
      "Error</a> | <a href=\"/../etc/passwd\">Path Traversal Test</a></p>";
inline const char* TESTING_INSTRUCTIONS_FOOTER = "</body></html>";

// Test section headers
inline const char* TEST_SUITE_HEADER = "Testing user-facing behavior, not internal implementation";
inline const char* TEST_AUTH_HEADER = "\n=== TESTING AUTHENTICATION BEHAVIOR ===\n";
inline const char* TEST_FILE_SERVING_HEADER = "\n=== TESTING FILE SERVING BEHAVIOR ===\n";
inline const char* TEST_ERROR_HANDLING_HEADER = "\n=== TESTING ERROR HANDLING (UNHAPPY PATHS) ===\n";
inline const char* TEST_INVALID_METHODS_HEADER = "\n=== TESTING INVALID HTTP METHODS (UNHAPPY PATHS) ===\n";
inline const char* TEST_MALFORMED_HEADER = "\n=== TESTING MALFORMED REQUESTS (UNHAPPY PATHS) ===\n";
inline const char* TEST_PATH_TRAVERSAL_HEADER = "\n=== TESTING PATH TRAVERSAL ATTACKS (UNHAPPY PATHS) ===\n";
inline const char* TEST_AUTH_FAILURES_HEADER = "\n=== TESTING AUTHENTICATION FAILURES (UNHAPPY PATHS) ===\n";

// Test content strings
inline const char* TESTING_GUIDE_TEXT = "Testing Guide";

// HTTP framing strings (kept here to avoid editor/formatter corruption)
inline const char* HTTP_CRLF = "\r\n";
inline const char* HTTP_HEADER_END = "\r\n\r\n";
inline const char* HTTP_REQUEST_LINE_SUFFIX = " HTTP/1.1\r\nHost: localhost\r\n";
inline const char* HTTP_AUTH_HEADER_PREFIX = "Authorization: Basic ";

// Additional HTTP constants for behavioral tests
inline const char* DOUBLE_CRLF = "\r\n\r\n";
inline const char* AUTH_ADMIN_SECRET = "Basic YWRtaW46c2VjcmV0";  // admin:secret
inline const char* AUTH_WRONG_PASS = "Basic d3JvbmdwYXNzd29yZA==";  // wrong:password
inline const char* AUTH_USER_PASS = "Basic dXNlcjpwYXNz";  // user:pass

// HTTP response templates
inline const char* HTTP_200_OK = "HTTP/1.1 200 OK";
inline const char* HTTP_206_PARTIAL = "HTTP/1.1 206 Partial Content";
inline const char* HTTP_400_BAD = "HTTP/1.1 400 Bad Request";
inline const char* HTTP_401_UNAUTHORIZED = "HTTP/1.1 401 Unauthorized";
inline const char* HTTP_403_FORBIDDEN = "HTTP/1.1 403 Forbidden";
inline const char* HTTP_404_NOT_FOUND = "HTTP/1.1 404 Not Found";
inline const char* HTTP_405_NOT_ALLOWED = "HTTP/1.1 405 Method Not Allowed";
inline const char* HTTP_416_RANGE_NOT_SATISFIABLE = "HTTP/1.1 416 Range Not Satisfiable";

// HTTP headers
inline const char* HEADER_CONTENT_TYPE = "Content-Type";
inline const char* HEADER_CONTENT_LENGTH = "Content-Length";
inline const char* HEADER_CONTENT_RANGE = "Content-Range";
inline const char* HEADER_WWW_AUTHENTICATE = "WWW-Authenticate";
inline const char* HEADER_AUTHORIZATION = "Authorization";
inline const char* HEADER_HOST = "Host";
inline const char* HEADER_RANGE = "Range";
inline const char* HEADER_ALLOW = "Allow";
inline const char* HEADER_CONNECTION = "Connection";
inline const char* HEADER_SERVER = "Server";
inline const char* HEADER_DATE = "Date";

// Header values
inline const char* AUTH_BASIC_REALM = "Basic realm=\"Test Area\"";
inline const char* TEXT_PLAIN = "text/plain";
inline const char* TEXT_HTML = "text/html";
inline const char* APPLICATION_OCTET_STREAM = "application/octet-stream";
inline const char* ALLOW_GET_HEAD = "GET, HEAD";
inline const char* CONNECTION_CLOSE = "close";

// Test file content
inline const char* TEST_FILE_CONTENT = "test file content";
inline const char* WELL_KNOWN_CONTENT = "well-known info";
inline const char* SECRET_GIT_CONTENT = "secret git data";
inline const char* LOG_DATA_CONTENT = "log data";
inline const char* CONFIG_DATA_CONTENT = "config data";
inline const char* HIDDEN_FILE_CONTENT = "hidden file";
inline const char* ENV_DATA_CONTENT = "env data";
inline const char* ESCAPE_CONTENT = "escape";

// Test paths
inline const char* PATH_TEST_TXT = "/test.txt";
inline const char* PATH_WELL_KNOWN_SECURITY = "/.well-known/security.txt";
inline const char* PATH_GIT_CONFIG = "/.git/config";
inline const char* PATH_HTPASSWD = "/.htpasswd";
inline const char* PATH_ENV = "/.env";
inline const char* PATH_SERVER_CONF = "/server.conf";
inline const char* PATH_ACCESS_LOG = "/access.log";
inline const char* PATH_LINK = "/link";
inline const char* PATH_NONEXISTENT = "/nonexistent.txt";
inline const char* PATH_TRAVERSAL = "/../../../etc/passwd";
inline const char* PATH_ENCODED = "/test%2Etxt";

} // namespace TestStringLiterals
