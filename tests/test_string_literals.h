#pragma once
// String literals for test_custom_file_server.cpp
// Separated to avoid auto-formatting corruption issues

namespace TestStringLiterals {

// Error HTML generation strings
inline const char* ERROR_HTML_BODY_STYLE = "body { font-family: Arial, sans-serif; margin: 40px; background: #f5f5f5; }";
inline const char* ERROR_HTML_CONTAINER_STYLE = ".error-container { max-width: 600px; margin: 0 auto; background: white; padding: 40px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }";
inline const char* ERROR_HTML_H1_STYLE = "h1 { color: #e74c3c; margin-bottom: 20px; }";
inline const char* ERROR_HTML_P_STYLE = "p { color: #555; line-height: 1.6; }";
inline const char* ERROR_HTML_LINK_STYLE = ".back-link { display: inline-block; margin-top: 20px; padding: 10px 20px; background: #3498db; color: white; text-decoration: none; border-radius: 4px; }";
inline const char* ERROR_HTML_LINK_HOVER_STYLE = ".back-link:hover { background: #2980b9; }";

// Auth required message
inline const char* AUTH_REQUIRED_MESSAGE = "This server requires authentication. Please provide valid credentials.";

// Testing instructions strings
inline const char* TESTING_INSTRUCTIONS_TITLE = "<!DOCTYPE html>\n<html><head><title>HttpFileServer - Testing Guide</title>";
inline const char* TESTING_INSTRUCTIONS_STYLE = "<style>body { font-family: Arial, sans-serif; margin: 20px; }</style></head><body>";
inline const char* TESTING_INSTRUCTIONS_HEADER = "<h1>🚀 HttpFileServer Testing Guide</h1>";
inline const char* TESTING_INSTRUCTIONS_INTRO = "<p>This is the testing interface for the CustomFileServer.</p>";
inline const char* TESTING_INSTRUCTIONS_AUTH = "<h2>🔐 Authentication</h2><p>Username: admin, Password: secret</p>";
inline const char* TESTING_INSTRUCTIONS_FILES = "<h2>📄 Test Files</h2><p><a href=\"/index.html\">index.html</a> | <a href=\"/style.css\">style.css</a> | <a href=\"/script.js\">script.js</a></p>";
inline const char* TESTING_INSTRUCTIONS_DIR = "<h2>📁 Directory Listing</h2><p><a href=\"/subdir/\">subdir/</a></p>";
inline const char* TESTING_INSTRUCTIONS_ACCESS = "<h2>🚫 Access Control</h2><p><a href=\"/config.conf\">config.conf</a> (should be blocked)</p>";
inline const char* TESTING_INSTRUCTIONS_ERROR = "<h2>❌ Error Testing</h2><p><a href=\"/nonexistent.html\">404 Error</a> | <a href=\"/../etc/passwd\">Path Traversal Test</a></p>";
inline const char* TESTING_INSTRUCTIONS_FOOTER = "</body></html>";

} // namespace TestStringLiterals
