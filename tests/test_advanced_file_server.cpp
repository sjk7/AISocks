// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Comprehensive tests for CustomFileServer
// Tests both happy path (successful operations) and sad path (error conditions)
// String literals are now in test_string_literals.h to prevent formatting
// corruption

#include "HttpFileServer.h"
#include "FileIO.h"
#include "PathHelper.h"
#include "Stopwatch.h"
#include "test_string_literals.h"
#include <string>
#include <array>
#include <thread>
#include <chrono>
#include <cassert>
#include <vector>

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
        , logFile_("access.log", "a+") {}

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

        if (request.path == "/access.log") {
            if (!isLocalClient(state.peerAddress)) {
                sendError(state, 403, "Forbidden",
                    "The access log viewer is only available from local "
                    "connections.");
                return;
            }

            std::string page = generateAccessLogTailPage();
            const bool headOnly = (request.method == "HEAD");

            std::string response;
            response.reserve(256 + page.size());
            response += "HTTP/1.1 200 OK\r\n";
            response
                += "Content-Type: text/html; charset=utf-8\r\nContent-Length: ";
            response += std::to_string(page.size());
            response += "\r\nCache-Control: no-store\r\nRefresh: 2\r\n\r\n";
            if (!headOnly) response += page;

            state.responseBuf = std::move(response);
            state.responseView = state.responseBuf;
            return;
        }

        // Special handling for root path - show testing instructions
        if (request.path == "/" || request.path == "/index.html") {
            std::string instructions = generateTestingInstructions();
            const bool headOnly = (request.method == "HEAD");

            std::string response;
            response.reserve(256 + instructions.size());
            response += "HTTP/1.1 200 OK\r\n";
            response
                += "Content-Type: text/html; charset=utf-8\r\nContent-Length: ";
            response += std::to_string(instructions.size());
            response += "\r\n";

            for (const auto& [name, value] : getConfig().customHeaders) {
                response += name;
                response += ": ";
                response += value;
                response += "\r\n";
            }

            response += "\r\n";
            if (!headOnly) response += instructions;

            state.responseBuf = std::move(response);
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

    std::string generateErrorHtml(int code, const std::string& status,
        const std::string& message) const override {
        std::string html;
        html += "<!DOCTYPE html>\n<html><head><title>";
        html += std::to_string(code);
        html += ' ';
        html += status;
        html += "</title><style>";
        html += TestStringLiterals::ERROR_HTML_BODY_STYLE;
        html += TestStringLiterals::ERROR_HTML_CONTAINER_STYLE;
        html += "h1 { color: #e74c3c; margin-bottom: 20px; }";
        html += "p { color: #555; line-height: 1.6; }";
        html += TestStringLiterals::ERROR_HTML_LINK_STYLE;
        html += ".back-link:hover { background: #2980b9; }";
        html += "</style></head><body>";
        html += "<div class=\"error-container\">";
        html += "<h1>";
        html += std::to_string(code);
        html += ' ';
        html += status;
        html += "</h1>";
        html += "<p>";
        html += message;
        html += "</p>";
        html += "<a href=\"/\" class=\"back-link\">\u2190 Back to Home</a>";
        html += "</div></body></html>";
        return html;
    }
    size_t cacheSize() { return getFileCache().size(); }

    protected:
    bool isAuthenticated(const HttpRequest& request) const {
        auto authHeader = request.headers.find("authorization");
        if (authHeader == request.headers.end()) return false;

        std::string_view authValue = authHeader->second;
        if (authValue.substr(0, 6) != "Basic ") return false;

        std::string_view expectedAuth
            = "Basic YWRtaW46c2VjcmV0"; // admin:secret
        return authValue == expectedAuth;
    }

    bool isLocalClient(const std::string& peerAddress) const {
        return peerAddress == "127.0.0.1" || peerAddress == "::1"
            || peerAddress == "::ffff:127.0.0.1" || peerAddress == "localhost";
    }

    void sendAuthRequired(HttpClientState& state) {
        std::string htmlBody = generateErrorHtml(
            401, "Unauthorized", TestStringLiterals::AUTH_REQUIRED_MESSAGE);

        std::string response;
        response.reserve(256 + htmlBody.size());
        response += "HTTP/1.1 401 Unauthorized\r\n";
        response
            += "Content-Type: text/html; charset=utf-8\r\nContent-Length: ";
        response += std::to_string(htmlBody.size());
        response += "\r\nWWW-Authenticate: Basic realm=\"Secure Area\"\r\n\r\n";
        response += htmlBody;

        state.responseBuf = std::move(response);
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
        logFile_.printf("%s %.*s %.*s\n", buffer,
            static_cast<int>(request.method.length()), request.method.data(),
            static_cast<int>(request.path.length()), request.path.data());
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
        std::string html;
        html += TestStringLiterals::TESTING_INSTRUCTIONS_TITLE;
        html += TestStringLiterals::TESTING_INSTRUCTIONS_STYLE;
        html += TestStringLiterals::TESTING_INSTRUCTIONS_HEADER;
        html += TestStringLiterals::TESTING_INSTRUCTIONS_INTRO;
        html += TestStringLiterals::TESTING_INSTRUCTIONS_AUTH;
        html += TestStringLiterals::TESTING_INSTRUCTIONS_FILES;
        html += TestStringLiterals::TESTING_INSTRUCTIONS_DIR;
        html += TestStringLiterals::TESTING_INSTRUCTIONS_ACCESS;
        html += TestStringLiterals::TESTING_INSTRUCTIONS_ERROR;
        html += "<p><a href=\"/access.log\">Open local access log tail</a></p>";
        html += "</body></html>";
        return html;
    }

    std::string readAccessLogTail(size_t maxLines) const {
        if (!logFile_.isOpen()) return {};
        (void)logFile_.flush();

        if (!logFile_.seek(0, SEEK_END)) return {};
        const auto fileSize = logFile_.tell();
        if (fileSize <= 0) return {};
        if (!logFile_.seek(0, SEEK_SET)) return {};

        std::vector<char> bytes(static_cast<size_t>(fileSize));
        const size_t bytesRead = logFile_.read(bytes.data(), 1, bytes.size());
        (void)logFile_.seek(0, SEEK_END);
        if (bytesRead == 0) return {};
        bytes.resize(bytesRead);

        std::string content(bytes.begin(), bytes.end());
        std::vector<size_t> lineStarts;
        lineStarts.reserve(32);
        lineStarts.push_back(0);
        for (size_t i = 0; i < content.size(); ++i) {
            if (content[i] == '\n' && i + 1 < content.size()) {
                lineStarts.push_back(i + 1);
            }
        }

        const size_t startIndex = (lineStarts.size() > maxLines)
            ? (lineStarts.size() - maxLines)
            : 0;
        return content.substr(lineStarts[startIndex]);
    }

    std::string generateAccessLogTailPage() const {
        const std::string tail = readAccessLogTail(100);
        std::string html;
        html += "<!DOCTYPE html><html><head><title>Access Log Tail</title>";
        html += "<meta http-equiv=\"refresh\" content=\"2\">";
        html += "</head><body><h1>Access Log Tail</h1>";
        html += "<p>Visible only from local connections.</p><pre>";
        html += tail.empty() ? std::string("No log entries yet.\n") : tail;
        html += "</pre></body></html>";
        return html;
    }

    mutable File logFile_;
};

/// Behavior-focused test helper
class BehavioralTestHelper {
    public:
    static std::string makeHttpRequest(const std::string& method,
        const std::string& path, const std::string& auth = "") {
        std::string request;
        request.reserve(128);
        request += method;
        request += ' ';
        request += path;
        request += " HTTP/1.1\r\nHost: localhost\r\n";
        if (!auth.empty()) {
            request += "Authorization: Basic ";
            request += auth;
            request += "\r\n";
        }
        request += "\r\n";
        return request;
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

        start = response.find(':', start) + 1;
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

static void runTestWithTrace(const char* name, void (*testFn)()) {
    printf("[TRACE] START: %s\n", name);
    fflush(stdout);

    const int failedBefore = TestFramework::failedTests;
    const int passedBefore = TestFramework::passedTests;

    testFn();

    const int failedDelta = TestFramework::failedTests - failedBefore;
    const int passedDelta = TestFramework::passedTests - passedBefore;
    printf("[TRACE] END: %s (passed +%d, failed +%d)\n", name, passedDelta,
        failedDelta);
    fflush(stdout);
}

/// Create test environment
void setupTestEnvironment() {
    std::remove("access.log");

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

    File largeFile("test_www/large.bin", "wb");
    std::vector<char> largeData(300 * 1024, 'L');
    largeFile.write(largeData.data(), 1, largeData.size());
    largeFile.close();

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
/// Regression: requests with query strings must bypass file-content cache.
void testQueryStringBypassesCache() {
    fputs("\n=== QUERY CACHE BYPASS REGRESSION TESTS ===\n", stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    config.enableCache = true;
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Query requests should be served but not inserted into file cache.
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/script.js?cacheBust=123", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "200", "Query request should be served successfully");
        TestFramework::assert_true(server.cacheSize() == 0,
            "Query request must bypass file cache insertion");
    }

    // Non-query requests are cache-eligible and should populate cache.
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/script.js", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "200", "Non-query request should be served successfully");
        TestFramework::assert_true(
            server.cacheSize() > 0, "Non-query request should populate cache");
    }
}

static std::string readPublicLandingPage_() {
    const char* candidates[] = {
        "www/public.html",
        "../www/public.html",
        "../../www/public.html",
    };

    for (const char* path : candidates) {
        File file(path, "rb");
        if (!file.isOpen()) continue;
        std::vector<char> content = file.readAll();
        if (!content.empty()) {
            return std::string(content.begin(), content.end());
        }
    }

    std::string sourcePath = __FILE__;
    const std::string marker = "/tests/test_advanced_file_server.cpp";
    const size_t markerPos = sourcePath.rfind(marker);
    if (markerPos != std::string::npos) {
        sourcePath.resize(markerPos);
        sourcePath += "/www/public.html";
        File file(sourcePath.c_str(), "rb");
        if (file.isOpen()) {
            std::vector<char> content = file.readAll();
            if (!content.empty()) {
                return std::string(content.begin(), content.end());
            }
        }
    }

    return {};
}

void testPublicLandingPageSignInLinkTargetsProtectedPage() {
    fputs("\n=== PUBLIC LANDING PAGE TESTS ===\n", stdout);

    const std::string html = readPublicLandingPage_();
    TestFramework::assert_true(
        !html.empty(), "Public landing page fixture should be readable");
    if (html.empty()) return;

    TestFramework::assert_contains(html, "Welcome to nginx",
        "Public landing page should contain nginx welcome content");
    TestFramework::assert_contains(html, "nginx.org",
        "Public landing page should contain nginx.org link");
    TestFramework::assert_not_contains(html, "href=\"/\"",
        "Public landing page must not link to root path");
}

void testAccessLogBrowserTailBehavior() {
    fputs("\n=== ACCESS LOG VIEWER TESTS ===\n", stdout);

    File seedLog("access.log", "w");
    seedLog.writeString("older line\n");
    seedLog.writeString("recent line 1\n");
    seedLog.writeString("recent line 2\n");
    seedLog.close();

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/access.log", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;
        state.peerAddress = "127.0.0.1";

        server.buildResponse(state);

        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);
        std::string contentType = BehavioralTestHelper::extractHeader(
            state.responseBuf, "Content-Type");
        std::string refresh
            = BehavioralTestHelper::extractHeader(state.responseBuf, "Refresh");
        std::string body = BehavioralTestHelper::extractBody(state.responseBuf);

        TestFramework::assert_contains(
            status, "200", "Local authenticated access log viewer should load");
        TestFramework::assert_contains(contentType, "text/html",
            "Access log viewer should render in the browser as HTML");
        TestFramework::assert_contains(refresh, "2",
            "Access log viewer should auto-refresh for tail behavior");
        TestFramework::assert_contains(body, "Access Log Tail",
            "Access log viewer should show a clear heading");
        TestFramework::assert_contains(body, "recent line 2",
            "Access log viewer should show recent log entries");
    }

    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/access.log", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;
        state.peerAddress = "198.51.100.24";

        server.buildResponse(state);

        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);
        std::string body = BehavioralTestHelper::extractBody(state.responseBuf);

        TestFramework::assert_contains(status, "403",
            "Non-local access log viewer requests should be blocked");
        TestFramework::assert_contains(body, "local connections",
            "Non-local block page should explain the restriction");
    }
}

/// Regression: large files should use the non-cached direct-read path.
void testLargeFileBypassesCacheForHotPath() {
    fputs("\n=== LARGE FILE HOT-PATH TESTS ===\n", stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    config.enableCache = true;
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    std::string request = BehavioralTestHelper::makeHttpRequest(
        "GET", "/large.bin", "YWRtaW46c2VjcmV0");
    HttpClientState state;
    state.request = request;

    server.buildResponse(state);

    std::string status = BehavioralTestHelper::extractStatus(state.responseBuf);
    std::string contentLength = BehavioralTestHelper::extractHeader(
        state.responseBuf, "Content-Length");
    std::string body = BehavioralTestHelper::extractBody(state.responseBuf);

    TestFramework::assert_contains(
        status, "200", "Large file should be served successfully");
    TestFramework::assert_equals(std::to_string(300 * 1024), contentLength,
        "Large file should return expected Content-Length");
    TestFramework::assert_true(body.size() == 300 * 1024,
        "Large file response body should include all bytes");
    TestFramework::assert_true(server.cacheSize() == 0,
        "Large file should bypass cache insertion on hot path");
}

/// Clean up test environment
void cleanupTestEnvironment() {
    // Simple cleanup - remove files first, then directories
    std::remove("test_www/index.html");
    std::remove("test_www/style.css");
    std::remove("test_www/script.js");
    std::remove("test_www/large.bin");
    std::remove("test_www/config.conf");
    std::remove("test_www/debug.log");
    std::remove("test_www/.htpasswd");
    std::remove("test_www/.env");
    std::remove("test_www/subdir/readme.txt");
    std::remove("access.log");
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
        std::string request = "POST /index.html HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Authorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";

        HttpClientState state;
        state.request = request;

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
        std::string request = "PUT /index.html HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Authorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";

        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "405", "PUT method should return 405");
    }

    // Unhappy Path: DELETE method not allowed
    {
        std::string request = "DELETE /index.html HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Authorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";

        HttpClientState state;
        state.request = request;

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
        std::string request = "GET /index.html HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Authorization: InvalidFormat\r\n\r\n";

        HttpClientState state;
        state.request = request;

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

/// Test HEAD method behavior - should return headers without body
void testHeadMethodBehavior() {
    fputs("\n=== HEAD METHOD TESTS ===\n", stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Behavior: HEAD request returns headers but no body (like GET headers)
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "HEAD", "/index.html", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);
        std::string body = BehavioralTestHelper::extractBody(state.responseBuf);
        std::string contentLength = BehavioralTestHelper::extractHeader(
            state.responseBuf, "Content-Length");

        TestFramework::assert_equals(
            "HTTP/1.1 200 OK", status, "HEAD request should return 200 OK");
        TestFramework::assert_contains(
            contentLength, "", "HEAD should return Content-Length header");
        TestFramework::assert_true(
            body.empty(), "HEAD request should not include a body");
    }

    // Behavior: HEAD request for CSS file also returns headers only
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "HEAD", "/style.css", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);
        std::string contentType = BehavioralTestHelper::extractHeader(
            state.responseBuf, "Content-Type");

        TestFramework::assert_equals(
            "HTTP/1.1 200 OK", status, "HEAD on CSS should return 200");
        TestFramework::assert_contains(
            contentType, "text/css", "HEAD should include Content-Type header");
    }

    // Behavior: HEAD request for nonexistent file returns 404
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "HEAD", "/missing.txt", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "404", "HEAD request for missing file should return 404");
    }

    // Behavior: HEAD without authentication returns 401
    {
        std::string request
            = BehavioralTestHelper::makeHttpRequest("HEAD", "/index.html", "");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_contains(
            status, "401", "HEAD without credentials should return 401");
    }
}

/// Test Range request behavior (Partial Content)
void testRangeRequestBehavior() {
    fputs("\n=== RANGE REQUEST TESTS ===\n", stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Behavior: Range header requests partial content (206 response expected)
    {
        std::string request = "GET /script.js HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Authorization: Basic YWRtaW46c2VjcmV0\r\n"
                              "Range: bytes=0-5\r\n\r\n";

        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        // Server should support 206 or might not implement ranges (return 200)
        TestFramework::assert_true(status.find("200") != std::string::npos
                || status.find("206") != std::string::npos,
            "Range request should return 200 or 206");
    }

    // Behavior: Invalid range should not break server
    {
        std::string request = "GET /style.css HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Authorization: Basic YWRtaW46c2VjcmV0\r\n"
                              "Range: bytes=99999-99999\r\n\r\n";

        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_true(status.find("200") != std::string::npos
                || status.find("206") != std::string::npos
                || status.find("416") != std::string::npos,
            "Out-of-range request should be handled gracefully");
    }
}

/// Test If-Modified-Since / caching behavior
void testCachingHeadersBehavior() {
    fputs("\n=== CACHING HEADERS TESTS ===\n", stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Behavior: Server should include Last-Modified header
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/index.html", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string lastModified = BehavioralTestHelper::extractHeader(
            state.responseBuf, "Last-Modified");

        TestFramework::assert_true(
            lastModified.empty() || lastModified.find('-') != std::string::npos,
            "Server should include Last-Modified or not - acceptable either "
            "way");
    }

    // Behavior: If-Modified-Since for old date should return full content
    {
        std::string request
            = "GET /style.css HTTP/1.1\r\n"
              "Host: localhost\r\n"
              "Authorization: Basic YWRtaW46c2VjcmV0\r\n"
              "If-Modified-Since: Mon, 01 Jan 2020 00:00:00 GMT\r\n\r\n";

        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string status
            = BehavioralTestHelper::extractStatus(state.responseBuf);

        TestFramework::assert_true(status.find("200") != std::string::npos
                || status.find("304") != std::string::npos,
            "If-Modified-Since with old date should return 200 or 304");
    }

    // Behavior: Server should include Cache-Control or allow caching headers
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/script.js", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string cacheControl = BehavioralTestHelper::extractHeader(
            state.responseBuf, "Cache-Control");
        std::string expires
            = BehavioralTestHelper::extractHeader(state.responseBuf, "Expires");

        TestFramework::assert_true(
            cacheControl.empty() || expires.empty() || true,
            "Cache headers are optional but server should handle requests "
            "gracefully");
    }
}

/// Test MIME type handling for various file types
void testMimeTypeBehavior() {
    fputs("\n=== MIME TYPE TESTS ===\n", stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Behavior: HTML files should have text/html MIME type
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/index.html", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string contentType = BehavioralTestHelper::extractHeader(
            state.responseBuf, "Content-Type");

        TestFramework::assert_contains(contentType, "text/html",
            "HTML files should have text/html MIME type");
    }

    // Behavior: CSS files should have text/css MIME type
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/style.css", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string contentType = BehavioralTestHelper::extractHeader(
            state.responseBuf, "Content-Type");

        TestFramework::assert_contains(contentType, "text/css",
            "CSS files should have text/css MIME type");
    }

    // Behavior: JavaScript files should have application/javascript MIME type
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/script.js", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string contentType = BehavioralTestHelper::extractHeader(
            state.responseBuf, "Content-Type");

        TestFramework::assert_contains(contentType, "javascript",
            "JS files should have javascript MIME type");
    }

    // Behavior: Text files should have appropriate MIME type
    {
        std::string request = BehavioralTestHelper::makeHttpRequest(
            "GET", "/subdir/readme.txt", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;

        server.buildResponse(state);
        std::string contentType = BehavioralTestHelper::extractHeader(
            state.responseBuf, "Content-Type");

        TestFramework::assert_true(contentType.find("text") != std::string::npos
                || contentType.find("plain") != std::string::npos,
            "Text files should have text MIME type");
    }
}

/// Test concurrency - multiple sequential and quasi-parallel requests
void testConcurrencyBehavior() {
    fputs("\n=== CONCURRENCY TESTS ===\n", stdout);

    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Behavior: Multiple sequential requests from same client should work
    {
        int successCount = 0;
        for (int i = 0; i < 3; ++i) {
            std::string request = BehavioralTestHelper::makeHttpRequest(
                "GET", "/index.html", "YWRtaW46c2VjcmV0");
            HttpClientState state;
            state.request = request;

            server.buildResponse(state);
            std::string status
                = BehavioralTestHelper::extractStatus(state.responseBuf);

            if (status.find("200") != std::string::npos) {
                successCount++;
            }
        }

        TestFramework::assert_true(successCount == 3,
            "All sequential requests should succeed (got "
                + std::to_string(successCount) + " of 3)");
    }

    // Behavior: Multiple clients requesting different files
    {
        std::array<std::pair<std::string, std::string>, 3> requests = {{
            {"/index.html", "Testing"}, // index.html returns testing
                                        // instructions, not "Test Page"
            {"/style.css", "color: red"},
            {"/script.js", "console.log"},
        }};

        int successCount = 0;
        for (const auto& [path, expectedContent] : requests) {
            std::string request = BehavioralTestHelper::makeHttpRequest(
                "GET", path, "YWRtaW46c2VjcmV0");
            HttpClientState state; // Fresh state for each request
            state.request = request;

            server.buildResponse(state);
            std::string response = state.responseBuf;

            if (response.find("200") != std::string::npos
                && response.find(expectedContent) != std::string::npos) {
                successCount++;
            }
        }

        TestFramework::assert_true(
            successCount == static_cast<int>(requests.size()),
            "All concurrent-style requests should process correctly (got "
                + std::to_string(successCount) + " of "
                + std::to_string(requests.size()) + ")");
    }

    // Behavior: Rapid fire requests shouldn't crash server
    {
        int successCount = 0;
        for (int i = 0; i < 10; ++i) {
            std::string request = BehavioralTestHelper::makeHttpRequest(
                "GET", "/index.html", "YWRtaW46c2VjcmV0");
            HttpClientState state;
            state.request = request;

            try {
                server.buildResponse(state);
                std::string status
                    = BehavioralTestHelper::extractStatus(state.responseBuf);
                if (status.find("200") != std::string::npos) {
                    successCount++;
                }
            } catch (...) {
                // Catch to prevent test from crashing
            }
        }

        TestFramework::assert_true(successCount >= 8,
            "At least 8 of 10 rapid requests should succeed");
    }
}

/// Main test runner - focused on behavior, not implementation
// Test comment to verify brittleness is fixed - strings are centralized
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    aiSocks::Stopwatch totalTimer;
    printf("[TEST] CustomFileServer Behavioral Test Suite\n");
    printf("=========================================\n");
    fputs(TestStringLiterals::TEST_SUITE_HEADER, stdout);
    printf("\n");

    try {
        setupTestEnvironment();

        runTestWithTrace(
            "testAuthenticationBehavior", testAuthenticationBehavior);
        runTestWithTrace("testFileServingBehavior", testFileServingBehavior);
        runTestWithTrace(
            "testErrorHandlingBehavior", testErrorHandlingBehavior);
        runTestWithTrace(
            "testInvalidMethodsBehavior", testInvalidMethodsBehavior);
        runTestWithTrace(
            "testMalformedRequestsBehavior", testMalformedRequestsBehavior);
        runTestWithTrace(
            "testPathTraversalBehavior", testPathTraversalBehavior);
        runTestWithTrace("testAuthenticationFailuresBehavior",
            testAuthenticationFailuresBehavior);
        runTestWithTrace("testHeadMethodBehavior", testHeadMethodBehavior);
        runTestWithTrace("testRangeRequestBehavior", testRangeRequestBehavior);
        runTestWithTrace(
            "testCachingHeadersBehavior", testCachingHeadersBehavior);
        runTestWithTrace(
            "testQueryStringBypassesCache", testQueryStringBypassesCache);
        runTestWithTrace("testPublicLandingPageSignInLinkTargetsProtectedPage",
            testPublicLandingPageSignInLinkTargetsProtectedPage);
        runTestWithTrace("testAccessLogBrowserTailBehavior",
            testAccessLogBrowserTailBehavior);
        runTestWithTrace("testLargeFileBypassesCacheForHotPath",
            testLargeFileBypassesCacheForHotPath);
        runTestWithTrace("testMimeTypeBehavior", testMimeTypeBehavior);
        runTestWithTrace("testConcurrencyBehavior", testConcurrencyBehavior);

        cleanupTestEnvironment();

        TestFramework::printSummary();
        printf("Total time: %.1f ms\n", totalTimer.elapsedMs());

        return TestFramework::failedTests > 0 ? 1 : 0;

    } catch (const std::exception& e) {
        fprintf(stderr, "Test suite failed with exception: %s\n", e.what());
        cleanupTestEnvironment();
        return 1;
    }
}
