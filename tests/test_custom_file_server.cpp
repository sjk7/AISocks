// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Comprehensive tests for CustomFileServer
// Tests both happy path (successful operations) and sad path (error conditions)
// String literals are now in test_string_literals.h to prevent formatting
// corruption

#include "HttpFileServer.h"
#include "FileIO.h"
#include "PathHelper.h"
#include "test_string_literals.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cassert>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace aiSocks;
using namespace std::chrono_literals;

// Include the CustomFileServer implementation from the example
// We need to define it here for testing since it's in the examples folder
class CustomFileServer : public HttpFileServer {
    public:
    explicit CustomFileServer(ServerBind bind, const Config& config = Config{})
        : HttpFileServer(std::move(bind), config)
        , logFile_("access.log", "a") {}

    ~CustomFileServer() { logFile_.close(); }

    public:
    void buildResponse(HttpClientState& state) override {
        auto request = HttpRequest::parse(state.request);

        // Validate request first (before authentication)
        if (!request.valid) {
            sendError(state, 400, "Bad Request", "Invalid HTTP request");
            return;
        }

        // Validate HTTP method (only GET and HEAD allowed)
        if (request.method != "GET" && request.method != "HEAD") {
            sendError(state, 405, "Method Not Allowed",
                "Only GET and HEAD methods are supported");
            return;
        }

        // Log valid requests
        logRequest(request, state);

        // Check authentication
        if (!isAuthenticated(request)) {
            sendAuthRequired(state);
            return;
        }

        // Special handling for root path - show testing instructions
        if (request.path == "/" || request.path == "/index.html") {
            std::string instructions = generateTestingInstructions();

            StringBuilder response;
            response.append("HTTP/1.1 200 OK\r\n");
            response.append(
                "Content-Type: text/html; charset=utf-8\r\nContent-Length: ");
            response.appendFormat("%zu", instructions.size());
            response.append("\r\n");

            for (const auto& [name, value] : getConfig().customHeaders) {
                response.append(name);
                response.append(": ");
                response.append(value);
                response.append("\r\n");
            }

            response.append("\r\n");
            response.append(instructions);

            state.responseBuf = response.toString();
            state.responseView = state.responseBuf;
            return;
        }

        HttpFileServer::buildResponse(state);
    }

    bool isAccessAllowed(
        const std::string& filePath, const FileInfo& fileInfo) const override {
        // Call base implementation first
        if (!HttpFileServer::isAccessAllowed(filePath, fileInfo)) {
            return false;
        }

        // Block sensitive file types
        std::string ext = getFileExtension(filePath);
        if (ext == ".conf" || ext == ".log" || ext == ".tmp") {
            return false;
        }

        return true;
    }

    std::string getMimeType(const std::string& filePath) const override {
        std::string ext = getFileExtension(filePath);

        if (ext == ".wasm") return "application/wasm";
        if (ext == ".ts") return "application/typescript";
        if (ext == ".jsx") return "text/jsx";
        if (ext == ".tsx") return "text/tsx";

        return HttpFileServer::getMimeType(filePath);
    }

    std::string generateErrorHtml(int code, const std::string& status,
        const std::string& message) const override {
        StringBuilder html;
        html.append("<!DOCTYPE html>\n<html><head><title>");
        html.appendFormat("%d %s", code, status.c_str());
        html.append("</title><style>");
        html.append(TestStringLiterals::ERROR_HTML_BODY_STYLE);
        html.append(TestStringLiterals::ERROR_HTML_CONTAINER_STYLE);
        html.append("h1 { color: #e74c3c; margin-bottom: 20px; }");
        html.append("p { color: #555; line-height: 1.6; }");
        html.append(TestStringLiterals::ERROR_HTML_LINK_STYLE);
        html.append(".back-link:hover { background: #2980b9; }");
        html.append("</style></head><body>");
        html.append("<div class=\"error-container\">");
        html.append("<h1>");
        html.appendFormat("%d %s", code, status.c_str());
        html.append("</h1>");
        html.append("<p>");
        html.append(message);
        html.append("</p>");
        html.append("<a href=\"/\" class=\"back-link\">← Back to Home</a>");
        html.append("</div></body></html>");
        return html.toString();
    }

    protected:
    bool isAuthenticated(const HttpRequest& request) const {
        auto authHeader = request.headers.find("authorization");
        if (authHeader == request.headers.end()) return false;

        const std::string& authValue = authHeader->second;
        if (authValue.substr(0, 6) != "Basic ") return false;

        std::string expectedAuth = "Basic YWRtaW46c2VjcmV0"; // admin:secret
        return authValue == expectedAuth;
    }

    void sendAuthRequired(HttpClientState& state) {
        std::string htmlBody = generateErrorHtml(
            401, "Unauthorized", TestStringLiterals::AUTH_REQUIRED_MESSAGE);

        StringBuilder response;
        response.append("HTTP/1.1 401 Unauthorized\r\n");
        response.append(
            "Content-Type: text/html; charset=utf-8\r\nContent-Length: ");
        response.appendFormat("%zu", htmlBody.size());
        response.append(
            "\r\nWWW-Authenticate: Basic realm=\"Secure Area\"\r\n\r\n");
        response.append(htmlBody);

        state.responseBuf = response.toString();
        state.responseView = state.responseBuf;
    }

    void logRequest(const HttpRequest& request, const HttpClientState& state) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        char buffer[32];
#ifdef _WIN32
        struct tm timeinfo = {};
        localtime_s(&timeinfo, &time_t);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
#else
        struct tm* timeinfo = localtime(&time_t);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
#endif

        (void)state; // Suppress unused parameter warning
        logFile_.printf(
            "%s %s %s\n", buffer, request.method.c_str(), request.path.c_str());
        logFile_.flush();
    }

    std::string getFileExtension(const std::string& filePath) const {
        size_t dotPos = filePath.find_last_of('.');
        if (dotPos != std::string::npos && dotPos < filePath.length() - 1) {
            return filePath.substr(dotPos);
        }
        return "";
    }

    std::string generateTestingInstructions() const {
        StringBuilder html;
        html.append(TestStringLiterals::TESTING_INSTRUCTIONS_TITLE);
        html.append(TestStringLiterals::TESTING_INSTRUCTIONS_STYLE);
        html.append(TestStringLiterals::TESTING_INSTRUCTIONS_HEADER);
        html.append(TestStringLiterals::TESTING_INSTRUCTIONS_INTRO);
        html.append(TestStringLiterals::TESTING_INSTRUCTIONS_AUTH);
        html.append(TestStringLiterals::TESTING_INSTRUCTIONS_FILES);
        html.append(TestStringLiterals::TESTING_INSTRUCTIONS_DIR);
        html.append(TestStringLiterals::TESTING_INSTRUCTIONS_ACCESS);
        html.append(TestStringLiterals::TESTING_INSTRUCTIONS_ERROR);
        html.append("</body></html>");
        return html.toString();
    }

    mutable File logFile_;
};

/// Behavior-focused test helper
class BehavioralTestHelper {
    public:
    static std::string makeHttpRequest(const std::string& method,
        const std::string& path, const std::string& auth = "") {
        StringBuilder request;
        request.append(method);
        request.append(" ");
        request.append(path);
        request.append(" HTTP/1.1\r\nHost: localhost\r\n");
        if (!auth.empty()) {
            request.append("Authorization: Basic ");
            request.append(auth);
            request.append("\r\n");
        }
        request.append("\r\n");
        return request.toString();
    }

    static std::string extractStatus(const std::string& response) {
        size_t end = response.find("\r\n");
        if (end != std::string::npos) {
            return response.substr(0, end);
        }
        return response;
    }

    static std::string extractBody(const std::string& response) {
        size_t headerEnd = response.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            return response.substr(headerEnd + 4);
        }
        return "";
    }

    static std::string extractHeader(
        const std::string& response, const std::string& headerName) {
        std::string search = headerName + ":";
        size_t start = response.find(search);
        if (start == std::string::npos) return "";

        start = response.find(":", start) + 1;
        while (start < response.size()
            && (response[start] == ' ' || response[start] == '\t')) {
            start++;
        }

        size_t end = response.find("\r\n", start);
        if (end == std::string::npos) end = response.size();

        return response.substr(start, end - start);
    }
};

/// Test framework helpers
class TestFramework {
    public:
    static void assert_true(bool condition, const std::string& message) {
        if (!condition) {
            fprintf(stderr, "[X] FAILED: %s\n", message.c_str());
            failedTests++;
        } else {
            printf("[OK] PASSED: %s\n", message.c_str());
            passedTests++;
        }
    }

    static void assert_equals(const std::string& expected,
        const std::string& actual, const std::string& message) {
        if (expected != actual) {
            fprintf(stderr, "[X] FAILED: %s\n", message.c_str());
            fprintf(stderr, "   Expected: \'%s\'\n", expected.c_str());
            fprintf(stderr, "   Actual:   \'%s\'\n", actual.c_str());
            failedTests++;
        } else {
            printf("[OK] PASSED: %s\n", message.c_str());
            passedTests++;
        }
    }

    static void assert_contains(const std::string& haystack,
        const std::string& needle, const std::string& message) {
        if (haystack.find(needle) == std::string::npos) {
            fprintf(stderr, "[X] FAILED: %s\n", message.c_str());
            fprintf(stderr, "   Expected to contain: \'%s\'\n", needle.c_str());
            fprintf(stderr, "   Actual string: \'%s\'\n", haystack.c_str());
            failedTests++;
        } else {
            printf("[OK] PASSED: %s\n", message.c_str());
            passedTests++;
        }
    }

    static void assert_not_contains(const std::string& haystack,
        const std::string& needle, const std::string& message) {
        if (haystack.find(needle) != std::string::npos) {
            fprintf(stderr, "[X] FAILED: %s\n", message.c_str());
            fprintf(stderr, "   Should NOT contain: \'%s\'\n", needle.c_str());
            failedTests++;
        } else {
            printf("[OK] PASSED: %s\n", message.c_str());
            passedTests++;
        }
    }

    static void printSummary() {
        printf("\n=== TEST SUMMARY ===\n");
        printf("Passed: %d\n", passedTests);
        printf("Failed: %d\n", failedTests);
        printf("Total:  %d\n", (passedTests + failedTests));

        if (failedTests == 0) {
            printf("[SUCCESS] ALL TESTS PASSED!\n");
        } else {
            printf("[ERROR] SOME TESTS FAILED!\n");
        }
    }

    static int passedTests;
    static int failedTests;
};

int TestFramework::passedTests = 0;
int TestFramework::failedTests = 0;

/// Create test environment
void setupTestEnvironment() {
    // Create test directories and files
#ifdef _WIN32
    _mkdir("test_www");
    _mkdir("test_www\\subdir");
#else
    mkdir("test_www", 0755);
    mkdir("test_www/subdir", 0755);
#endif

    // Create test files
    File htmlFile("test_www/index.html", "w");
    htmlFile.writeString("<html><body><h1>Test Page</h1></body></html>");
    htmlFile.close();

    File cssFile("test_www/style.css", "w");
    cssFile.writeString("body { color: red; }");
    cssFile.close();

    File jsFile("test_www/script.js", "w");
    jsFile.writeString("console.log('Hello World');");
    jsFile.close();

    File textFile("test_www/subdir/readme.txt", "w");
    textFile.writeString("This is a readme file.");
    textFile.close();

    // Create blocked files (should return 403)
    File confFile("test_www/config.conf", "w");
    confFile.writeString("server.port=8080");
    confFile.close();

    File logFile("test_www/debug.log", "w");
    logFile.writeString("Debug log entry");
    logFile.close();

    // Create dotfiles (should return 403, not 404)
    File htpasswdFile("test_www/.htpasswd", "w");
    htpasswdFile.writeString("admin:$apr1$...");
    htpasswdFile.close();

    File envFile("test_www/.env", "w");
    envFile.writeString("SECRET_KEY=abc123");
    envFile.close();
}

/// Clean up test environment
void cleanupTestEnvironment() {
    // Simple cleanup - remove files first, then directories
    std::remove("test_www/index.html");
    std::remove("test_www/style.css");
    std::remove("test_www/script.js");
    std::remove("test_www/config.conf");
    std::remove("test_www/debug.log");
    std::remove("test_www/.htpasswd");
    std::remove("test_www/.env");
    std::remove("test_www/subdir/readme.txt");
#ifdef _WIN32
    _rmdir("test_www\\subdir");
    _rmdir("test_www");
#else
    rmdir("test_www/subdir");
    rmdir("test_www");
#endif
    std::remove("test_access.log");
}

/// Test authentication behavior - what users experience
void testAuthenticationBehavior() {
    fputs(TestStringLiterals::TEST_AUTH_HEADER, stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Behavior: User with correct credentials can access the server
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string response = state.responseBuf;
        std::string status = BehavioralTestHelper::extractStatus(response);
        std::string body = BehavioralTestHelper::extractBody(response);

        TestFramework::assert_equals(
            "HTTP/1.1 200 OK", status, "Valid credentials should grant access");
        TestFramework::assert_contains(body,
            TestStringLiterals::TESTING_GUIDE_TEXT,
            "Should show testing interface");
    }

    // Behavior: User with wrong credentials gets access denied
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/", "d3Jvbmc6Y3JlZGVudGlhbHM=");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string response = state.responseBuf;
        std::string status = BehavioralTestHelper::extractStatus(response);
        std::string body = BehavioralTestHelper::extractBody(response);

        TestFramework::assert_equals("HTTP/1.1 401 Unauthorized", status,
            "Wrong credentials should deny access");
        TestFramework::assert_contains(
            body, "Unauthorized", "Should show access denied page");
        TestFramework::assert_contains(
            body, "authentication", "Should explain authentication required");
    }

    // Behavior: User without credentials gets prompted to login
    {
        std::string request
            = BehavioralTestHelper::makeHttpRequest("GET", "/", "");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string response = state.responseBuf;
        std::string status = BehavioralTestHelper::extractStatus(response);
        std::string authHeader
            = BehavioralTestHelper::extractHeader(response, "WWW-Authenticate");

        TestFramework::assert_equals("HTTP/1.1 401 Unauthorized", status,
            "No credentials should prompt for login");
        TestFramework::assert_contains(
            authHeader, "Basic realm", "Should include browser login prompt");
    }
}

/// Test file serving behavior - what users experience when requesting files
void testFileServingBehavior() {
    fputs(TestStringLiterals::TEST_FILE_SERVING_HEADER, stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Behavior: User can access HTML files with proper content type
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/index.html", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string response = state.responseBuf;
        std::string status = BehavioralTestHelper::extractStatus(response);
        std::string contentType
            = BehavioralTestHelper::extractHeader(response, "Content-Type");
        std::string body = BehavioralTestHelper::extractBody(response);

        TestFramework::assert_equals(
            "HTTP/1.1 200 OK", status, "HTML file should be accessible");
        TestFramework::assert_contains(
            contentType, "text/html", "Should serve with HTML MIME type");
        TestFramework::assert_contains(
            contentType, "charset=utf-8", "Should include UTF-8 charset");
        TestFramework::assert_contains(body,
            TestStringLiterals::TESTING_GUIDE_TEXT,
            "Should show testing interface (index.html redirects to testing "
            "guide)");
    }

    // Behavior: User can access CSS files for styling
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/style.css", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string response = state.responseBuf;
        std::string status = BehavioralTestHelper::extractStatus(response);
        std::string contentType
            = BehavioralTestHelper::extractHeader(response, "Content-Type");
        std::string body = BehavioralTestHelper::extractBody(response);

        TestFramework::assert_equals(
            "HTTP/1.1 200 OK", status, "CSS file should be accessible");
        TestFramework::assert_contains(
            contentType, "text/css", "Should serve with CSS MIME type");
        TestFramework::assert_contains(
            body, "color: red", "Should contain styling rules");
    }

    // Behavior: User can access JavaScript files for functionality
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/script.js", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string response = state.responseBuf;
        std::string status = BehavioralTestHelper::extractStatus(response);
        std::string contentType
            = BehavioralTestHelper::extractHeader(response, "Content-Type");
        std::string body = BehavioralTestHelper::extractBody(response);

        TestFramework::assert_equals(
            "HTTP/1.1 200 OK", status, "JavaScript file should be accessible");
        TestFramework::assert_contains(contentType, "application/javascript",
            "Should serve with JS MIME type");
        TestFramework::assert_contains(
            body, "console.log", "Should contain JavaScript code");
    }
}

/// Test error handling behavior - unhappy paths
void testErrorHandlingBehavior() {
    fputs(TestStringLiterals::TEST_ERROR_HANDLING_HEADER, stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Unhappy Path: File not found (404)
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/nonexistent.html", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);
        std::string body = BehavioralTestHelper::extractBody(state.responseBuf);

        TestFramework::assert_contains(
            status, "404", "Nonexistent file should return 404");
        TestFramework::assert_contains(
            body, "Not Found", "404 page should explain file not found");
    }

    // Unhappy Path: Directory without index file and listing disabled
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/subdir/", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(status, "403",
            "Directory without index should return 403 when listing disabled");
    }

    // Unhappy Path: Blocked file type (.conf)
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/config.conf", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);
        std::string body = BehavioralTestHelper::extractBody(state.responseBuf);

        TestFramework::assert_contains(
            status, "403", "Blocked file type should return 403");
        TestFramework::assert_contains(
            body, "Forbidden", "Should explain access is forbidden");
    }

    // Unhappy Path: Blocked file type (.log)
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/debug.log", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "403", "Log files should be blocked");
    }

    // Unhappy Path: Dotfile (.htpasswd) should return 403, not 404
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/.htpasswd", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);
        std::string body = BehavioralTestHelper::extractBody(state.responseBuf);

        TestFramework::assert_contains(
            status, "403", "Dotfiles should return 403 Forbidden, not 404");
        TestFramework::assert_contains(
            body, "Forbidden", "Should explain access is forbidden");
        TestFramework::assert_not_contains(body, "Not Found",
            "Should not say 'Not Found' for existing dotfiles");
    }

    // Unhappy Path: Dotfile (.env) should return 403, not 404
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/.env", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "403", ".env files should return 403 Forbidden, not 404");
    }

    // Unhappy Path: Nonexistent dotfile should return 404 (file doesn't exist)
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/.nonexistent", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);
        std::string body = BehavioralTestHelper::extractBody(state.responseBuf);

        TestFramework::assert_contains(
            status, "404", "Nonexistent dotfile should return 404 Not Found");
        TestFramework::assert_contains(
            body, "Not Found", "Should explain file not found");
        TestFramework::assert_not_contains(body, "Forbidden",
            "Should not say 'Forbidden' for nonexistent files");
    }
}

/// Test invalid HTTP methods - unhappy paths
void testInvalidMethodsBehavior() {
    fputs(TestStringLiterals::TEST_INVALID_METHODS_HEADER, stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Unhappy Path: POST method not allowed
    {
        StringBuilder request;
        request.append("POST /index.html HTTP/1.1\r\n");
        request.append("Host: localhost\r\n");
        request.append("Authorization: Basic YWRtaW46c2VjcmV0\r\n");
        request.append("\r\n");

        HttpClientState state;
        state.request = request.toString();

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);
        std::string body = BehavioralTestHelper::extractBody(state.responseBuf);

        TestFramework::assert_contains(
            status, "405", "POST method should return 405");
        TestFramework::assert_contains(
            body, "Method Not Allowed", "Should explain method not allowed");
    }

    // Unhappy Path: PUT method not allowed
    {
        StringBuilder request;
        request.append("PUT /index.html HTTP/1.1\r\n");
        request.append("Host: localhost\r\n");
        request.append("Authorization: Basic YWRtaW46c2VjcmV0\r\n");
        request.append("\r\n");

        HttpClientState state;
        state.request = request.toString();

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "405", "PUT method should return 405");
    }

    // Unhappy Path: DELETE method not allowed
    {
        StringBuilder request;
        request.append("DELETE /index.html HTTP/1.1\r\n");
        request.append("Host: localhost\r\n");
        request.append("Authorization: Basic YWRtaW46c2VjcmV0\r\n");
        request.append("\r\n");

        HttpClientState state;
        state.request = request.toString();

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "405", "DELETE method should return 405");
    }
}

/// Test malformed requests - unhappy paths
void testMalformedRequestsBehavior() {
    fputs(TestStringLiterals::TEST_MALFORMED_HEADER, stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Unhappy Path: Invalid HTTP request (no HTTP version)
    {
        std::string request = "GET /index.html\r\nHost: localhost\r\n\r\n";
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "400", "Malformed request should return 400");
    }

    // Unhappy Path: Empty request
    {
        std::string request = "";
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "400", "Empty request should return 400");
    }

    // Unhappy Path: Request with only whitespace
    {
        std::string request = "   \r\n\r\n";
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_true(status.find("400") != std::string::npos
                || status.find("405") != std::string::npos,
            "Whitespace-only request should return 400 or 405");
    }
}

/// Test path traversal attempts - unhappy paths (security)
void testPathTraversalBehavior() {
    fputs(TestStringLiterals::TEST_PATH_TRAVERSAL_HEADER, stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Unhappy Path: Basic path traversal with ../
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/../etc/passwd", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);
        std::string body = BehavioralTestHelper::extractBody(state.responseBuf);

        // Print actual status for debugging
        printf("   DEBUG: Path traversal /../etc/passwd returns: %s\n",
            status.c_str());

        TestFramework::assert_true(status.find("403") != std::string::npos
                || status.find("404") != std::string::npos,
            "Path traversal should return 403 or 404 (got: " + status + ")");
        TestFramework::assert_not_contains(
            body, "root:", "Should not leak system files");
    }

    // Unhappy Path: URL-encoded path traversal
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/%2e%2e/secret/data", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(status, "403",
            "URL-encoded path traversal should return 403 Forbidden");
    }

    // Unhappy Path: Multiple levels of traversal
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/../../../../../../etc/passwd", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "403", "Deep path traversal should return 403 Forbidden");
    }
}

/// Test authentication failures - unhappy paths
void testAuthenticationFailuresBehavior() {
    fputs(TestStringLiterals::TEST_AUTH_FAILURES_HEADER, stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Unhappy Path: Wrong password
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/index.html", "YWRtaW46d3JvbmdwYXNz"); // admin:wrongpass
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);
        std::string body = BehavioralTestHelper::extractBody(state.responseBuf);

        TestFramework::assert_contains(
            status, "401", "Wrong password should return 401");
        TestFramework::assert_contains(
            body, "Unauthorized", "Should show unauthorized message");
        TestFramework::assert_not_contains(body, "Test Page",
            "Should not serve content with wrong credentials");
    }

    // Unhappy Path: Wrong username
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/index.html", "dXNlcjpzZWNyZXQ="); // user:secret
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "401", "Wrong username should return 401");
    }

    // Unhappy Path: Malformed auth header
    {
        StringBuilder request;
        request.append("GET /index.html HTTP/1.1\r\n");
        request.append("Host: localhost\r\n");
        request.append("Authorization: InvalidFormat\r\n");
        request.append("\r\n");

        HttpClientState state;
        state.request = request.toString();

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "401", "Malformed auth should return 401");
    }

    // Unhappy Path: Empty auth credentials
    {
        std::string request
            = BehavioralTestHelper::makeHttpRequest("GET", "/index.html", "");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);
        std::string authHeader = BehavioralTestHelper::extractHeader(
            state.responseBuf, "WWW-Authenticate");

        TestFramework::assert_contains(
            status, "401", "No credentials should return 401");
        TestFramework::assert_contains(
            authHeader, "Basic", "Should prompt for Basic auth");
    }
}

/// Main test runner - focused on behavior, not implementation
// Test comment to verify brittleness is fixed - strings are centralized
int main() {
    printf("[TEST] CustomFileServer Behavioral Test Suite\n");
    printf("=========================================\n");
    fputs(TestStringLiterals::TEST_SUITE_HEADER, stdout);
    printf("\n");

    try {
        setupTestEnvironment();

        testAuthenticationBehavior();
        testFileServingBehavior();
        testErrorHandlingBehavior();
        testInvalidMethodsBehavior();
        testMalformedRequestsBehavior();
        testPathTraversalBehavior();
        testAuthenticationFailuresBehavior();

        cleanupTestEnvironment();

        TestFramework::printSummary();

        return TestFramework::failedTests > 0 ? 1 : 0;

    } catch (const std::exception& e) {
        fprintf(stderr, "Test suite failed with exception: %s\n", e.what());
        cleanupTestEnvironment();
        return 1;
    }
}
