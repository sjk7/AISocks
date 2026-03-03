// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Comprehensive tests for CustomFileServer
// Tests both happy path (successful operations) and sad path (error conditions)

#include "HttpFileServer.h"
#include "FileIO.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <cassert>

using namespace aiSocks;
using namespace std::chrono_literals;

// Include the CustomFileServer implementation from the example
// We need to define it here for testing since it's in the examples folder
class CustomFileServer : public HttpFileServer {
public:
    explicit CustomFileServer(ServerBind bind, const Config& config = Config{}) 
        : HttpFileServer(std::move(bind), config), logFile_("access.log", "a") {}
    
    ~CustomFileServer() { logFile_.close(); }
    
protected:
    void buildResponse(HttpClientState& state) override {
        auto request = HttpRequest::parse(state.request);
        
        if (request.valid) {
            logRequest(request, state);
        }
        
        if (!isAuthenticated(request)) {
            sendAuthRequired(state);
            return;
        }
        
        // Special handling for root path - show testing instructions
        if (request.path == "/" || request.path == "/index.html") {
            std::string instructions = generateTestingInstructions();
            
            StringBuilder response;
            response.append("HTTP/1.1 200 OK\r\n");
            response.append("Content-Type: text/html; charset=utf-8\r\nContent-Length: ");
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
    
    std::string getMimeType(const std::string& filePath) const override {
        std::string ext = getFileExtension(filePath);
        
        if (ext == ".wasm") return "application/wasm";
        if (ext == ".ts") return "application/typescript";
        if (ext == ".jsx") return "text/jsx";
        if (ext == ".tsx") return "text/tsx";
        
        return HttpFileServer::getMimeType(filePath);
    }
    
    std::string generateErrorHtml(int code, const std::string& status, const std::string& message) const override {
        StringBuilder html;
        html.append("<!DOCTYPE html>\n<html><head><title>");
        html.appendFormat("%d %s", code, status.c_str());
        html.append("</title><style>");
        html.append("body { font-family: Arial, sans-serif; margin: 40px; background: #f5f5f5; }");
        html.append(".error-container { max-width: 600px; margin: 0 auto; background: white; padding: 40px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }");
        html.append("h1 { color: #e74c3c; margin-bottom: 20px; }");
        html.append("p { color: #555; line-height: 1.6; }");
        html.append(".back-link { display: inline-block; margin-top: 20px; padding: 10px 20px; background: #3498db; color: white; text-decoration: none; border-radius: 4px; }");
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
    
    // Make test class a friend to access protected members
    friend class TestCustomFileServer;
    
    void sendAuthRequired(HttpClientState& state) {
        std::string htmlBody = generateErrorHtml(401, "Unauthorized", 
            "This server requires authentication. Please provide valid credentials.");
        
        StringBuilder response;
        response.append("HTTP/1.1 401 Unauthorized\r\n");
        response.append("Content-Type: text/html; charset=utf-8\r\nContent-Length: ");
        response.appendFormat("%zu", htmlBody.size());
        response.append("\r\nWWW-Authenticate: Basic realm=\"Secure Area\"\r\n\r\n");
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
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
#else
        struct tm* timeinfo = std::localtime(&time_t);
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
#endif
        
        (void)state; // Suppress unused parameter warning
        logFile_.printf("%s %s %s\n", buffer, request.method.c_str(), request.path.c_str());
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
        html.append("<!DOCTYPE html>\n<html><head><title>HttpFileServer - Testing Guide</title>");
        html.append("<style>body { font-family: Arial, sans-serif; margin: 20px; }</style></head><body>");
        html.append("<h1>🚀 HttpFileServer Testing Guide</h1>");
        html.append("<p>This is the testing interface for the CustomFileServer.</p>");
        html.append("<h2>🔐 Authentication</h2><p>Username: admin, Password: secret</p>");
        html.append("<h2>📄 Test Files</h2><p><a href=\"/index.html\">index.html</a> | <a href=\"/style.css\">style.css</a> | <a href=\"/script.js\">script.js</a></p>");
        html.append("<h2>📁 Directory Listing</h2><p><a href=\"/subdir/\">subdir/</a></p>");
        html.append("<h2>🚫 Access Control</h2><p><a href=\"/config.conf\">config.conf</a> (should be blocked)</p>");
        html.append("<h2>❌ Error Testing</h2><p><a href=\"/nonexistent.html\">404 Error</a> | <a href=\"/../etc/passwd\">Path Traversal Test</a></p>");
        html.append("</body></html>");
        return html.toString();
    }
    
    mutable File logFile_;
};

/// Test helper class that extends CustomFileServer for testing
class TestCustomFileServer : public CustomFileServer {
public:
    using CustomFileServer::CustomFileServer;
    
    // Expose protected methods for testing
    using CustomFileServer::sendError;
    using CustomFileServer::isAuthenticated;
    using CustomFileServer::resolveFilePath;
    using CustomFileServer::getFileInfo;
    using CustomFileServer::getMimeType;
    
    // Test helper to get last response
    std::string getLastResponse() const { return lastResponse_; }
    void clearLastResponse() { lastResponse_.clear(); }
    
    // Public method to trigger buildResponse for testing
    void testBuildResponse(HttpClientState& state) {
        buildResponse(state);
        lastResponse_ = state.responseBuf;
    }
    
protected:
    void buildResponse(HttpClientState& state) override {
        CustomFileServer::buildResponse(state);
        lastResponse_ = state.responseBuf;
    }
    
private:
    std::string lastResponse_;
};

/// Test framework helpers
class TestFramework {
public:
    static void assert_true(bool condition, const std::string& message) {
        if (!condition) {
            std::cerr << "❌ FAILED: " << message << std::endl;
            failedTests++;
        } else {
            std::cout << "✅ PASSED: " << message << std::endl;
            passedTests++;
        }
    }
    
    static void assert_equals(const std::string& expected, const std::string& actual, const std::string& message) {
        if (expected != actual) {
            std::cerr << "❌ FAILED: " << message << std::endl;
            std::cerr << "   Expected: '" << expected << "'" << std::endl;
            std::cerr << "   Actual:   '" << actual << "'" << std::endl;
            failedTests++;
        } else {
            std::cout << "✅ PASSED: " << message << std::endl;
            passedTests++;
        }
    }
    
    static void assert_contains(const std::string& haystack, const std::string& needle, const std::string& message) {
        if (haystack.find(needle) == std::string::npos) {
            std::cerr << "❌ FAILED: " << message << std::endl;
            std::cerr << "   Expected to contain: '" << needle << "'" << std::endl;
            std::cerr << "   Actual string: '" << haystack << "'" << std::endl;
            failedTests++;
        } else {
            std::cout << "✅ PASSED: " << message << std::endl;
            passedTests++;
        }
    }
    
    static void printSummary() {
        std::cout << "\n=== TEST SUMMARY ===" << std::endl;
        std::cout << "Passed: " << passedTests << std::endl;
        std::cout << "Failed: " << failedTests << std::endl;
        std::cout << "Total:  " << (passedTests + failedTests) << std::endl;
        
        if (failedTests == 0) {
            std::cout << "🎉 ALL TESTS PASSED!" << std::endl;
        } else {
            std::cout << "💥 SOME TESTS FAILED!" << std::endl;
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
    std::filesystem::create_directories("test_www");
    std::filesystem::create_directories("test_www/subdir");
    
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
}

/// Clean up test environment
void cleanupTestEnvironment() {
    std::filesystem::remove_all("test_www");
    std::filesystem::remove("test_access.log");
}

/// Test authentication functionality
void testAuthentication() {
    std::cout << "\n=== TESTING AUTHENTICATION ===" << std::endl;
    
    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    TestCustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);
    
    // Test valid authentication
    {
        std::string authRequest = "GET / HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = authRequest;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 200 OK", "Valid authentication should succeed");
        TestFramework::assert_contains(response, "HttpFileServer - Testing Guide", "Should show testing instructions");
    }
    
    // Test invalid authentication
    {
        std::string authRequest = "GET / HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic d3Jvbmc6Y3JlZGVudGlhbHM=\r\n\r\n";
        HttpClientState state;
        state.request = authRequest;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 401 Unauthorized", "Invalid authentication should fail");
        TestFramework::assert_contains(response, "WWW-Authenticate: Basic realm=\"Secure Area\"", "Should include auth challenge");
    }
    
    // Test missing authentication
    {
        std::string noAuthRequest = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        HttpClientState state;
        state.request = noAuthRequest;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 401 Unauthorized", "Missing authentication should fail");
    }
}

/// Test file serving functionality
void testFileServing() {
    std::cout << "\n=== TESTING FILE SERVING ===" << std::endl;
    
    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    TestCustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);
    
    // Test serving HTML file
    {
        std::string request = "GET /index.html HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 200 OK", "HTML file should be served successfully");
        TestFramework::assert_contains(response, "Content-Type: text/html; charset=utf-8", "Should have correct MIME type with charset");
        TestFramework::assert_contains(response, "<html><body><h1>Test Page</h1></body></html>", "Should serve correct file content");
    }
    
    // Test serving CSS file
    {
        std::string request = "GET /style.css HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 200 OK", "CSS file should be served successfully");
        TestFramework::assert_contains(response, "Content-Type: text/css; charset=utf-8", "Should have correct CSS MIME type");
        TestFramework::assert_contains(response, "body { color: red; }", "Should serve correct CSS content");
    }
    
    // Test serving JavaScript file
    {
        std::string request = "GET /script.js HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 200 OK", "JavaScript file should be served successfully");
        TestFramework::assert_contains(response, "Content-Type: application/javascript; charset=utf-8", "Should have correct JS MIME type");
        TestFramework::assert_contains(response, "console.log('Hello World');", "Should serve correct JS content");
    }
}

/// Test directory listing functionality
void testDirectoryListing() {
    std::cout << "\n=== TESTING DIRECTORY LISTING ===" << std::endl;
    
    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    config.enableDirectoryListing = true;
    TestCustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);
    
    // Test root directory (should show testing instructions)
    {
        std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 200 OK", "Root directory should return testing instructions");
        TestFramework::assert_contains(response, "HttpFileServer Testing Guide", "Should show testing guide title");
    }
    
    // Test subdirectory listing
    {
        std::string request = "GET /subdir/ HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 200 OK", "Subdirectory should list successfully");
        TestFramework::assert_contains(response, "Directory listing:", "Should show directory listing");
        TestFramework::assert_contains(response, "readme.txt", "Should show readme.txt in listing");
    }
}

/// Test access control (blocked files)
void testAccessControl() {
    std::cout << "\n=== TESTING ACCESS CONTROL ===" << std::endl;
    
    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    TestCustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);
    
    // Test blocked .conf file
    {
        std::string request = "GET /config.conf HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 403 Forbidden", ".conf file should be blocked");
        TestFramework::assert_contains(response, "Access Denied", "Should show access denied message");
    }
    
    // Test blocked .log file
    {
        std::string request = "GET /debug.log HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 403 Forbidden", ".log file should be blocked");
    }
    
    // Test allowed file
    {
        std::string request = "GET /index.html HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 200 OK", "HTML file should be allowed");
    }
}

/// Test security features (path traversal protection)
void testSecurityFeatures() {
    std::cout << "\n=== TESTING SECURITY FEATURES ===" << std::endl;
    
    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    TestCustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);
    
    // Test path traversal attack
    {
        std::string request = "GET /../etc/passwd HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 400 Bad Request", "Path traversal should be blocked");
        TestFramework::assert_contains(response, "Path traversal not allowed", "Should show traversal error");
    }
    
    // Test URL-encoded path traversal attack
    {
        std::string request = "GET /%2e%2e%2f%2e%2e%2fetc%2fpasswd HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 400 Bad Request", "URL-encoded path traversal should be blocked");
    }
    
    // Test absolute path attack
    {
        std::string request = "GET /etc/passwd HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 400 Bad Request", "Absolute path should be blocked");
    }
}

/// Test error handling
void testErrorHandling() {
    std::cout << "\n=== TESTING ERROR HANDLING ===" << std::endl;
    
    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    TestCustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);
    
    // Test 404 Not Found
    {
        std::string request = "GET /nonexistent.html HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 404 Not Found", "Non-existent file should return 404");
        TestFramework::assert_contains(response, "File not found", "Should show not found message");
    }
    
    // Test invalid HTTP method
    {
        std::string request = "POST / HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 405 Method Not Allowed", "Invalid method should return 405");
    }
    
    // Test malformed HTTP request
    {
        std::string request = "INVALID REQUEST";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
        std::string response = server.getLastResponse();
        
        TestFramework::assert_contains(response, "HTTP/1.1 400 Bad Request", "Malformed request should return 400");
    }
}

/// Test MIME type detection
void testMimeTypeDetection() {
    std::cout << "\n=== TESTING MIME TYPE DETECTION ===" << std::endl;
    
    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    TestCustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);
    
    // Test various file extensions
    TestFramework::assert_equals("text/html", server.getMimeType("test.html"), "HTML MIME type");
    TestFramework::assert_equals("text/css", server.getMimeType("style.css"), "CSS MIME type");
    TestFramework::assert_equals("application/javascript", server.getMimeType("script.js"), "JavaScript MIME type");
    TestFramework::assert_equals("application/json", server.getMimeType("data.json"), "JSON MIME type");
    TestFramework::assert_equals("image/png", server.getMimeType("image.png"), "PNG MIME type");
    TestFramework::assert_equals("image/jpeg", server.getMimeType("photo.jpg"), "JPEG MIME type");
    TestFramework::assert_equals("application/wasm", server.getMimeType("module.wasm"), "WASM MIME type (custom)");
    TestFramework::assert_equals("application/typescript", server.getMimeType("app.ts"), "TypeScript MIME type (custom)");
    TestFramework::assert_equals("application/octet-stream", server.getMimeType("unknown.xyz"), "Default MIME type");
}

/// Test file path resolution
void testFilePathResolution() {
    std::cout << "\n=== TESTING FILE PATH RESOLUTION ===" << std::endl;
    
    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    TestCustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);
    
    // Test normal path resolution
    TestFramework::assert_equals("test_www/index.html", server.resolveFilePath("/index.html"), "Normal path resolution");
    TestFramework::assert_equals("test_www/subdir/file.txt", server.resolveFilePath("/subdir/file.txt"), "Subdirectory path resolution");
    
    // Test URL decoding
    TestFramework::assert_equals("test_www/hello world.txt", server.resolveFilePath("/hello%20world.txt"), "URL decoding");
    TestFramework::assert_equals("test_www/测试.txt", server.resolveFilePath("/%E6%B5%8B%E8%AF%95.txt"), "UTF-8 URL decoding");
    
    // Test query string removal
    TestFramework::assert_equals("test_www/index.html", server.resolveFilePath("/index.html?param=value"), "Query string removal");
    TestFramework::assert_equals("test_www/index.html", server.resolveFilePath("/index.html#fragment"), "Fragment removal");
}

/// Test logging functionality
void testLogging() {
    std::cout << "\n=== TESTING LOGGING FUNCTIONALITY ===" << std::endl;
    
    // Remove existing log file
    std::filesystem::remove("test_access.log");
    
    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    TestCustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);
    
    // Make a request to generate log entry
    {
        std::string request = "GET /index.html HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n";
        HttpClientState state;
        state.request = request;
        
        server.testBuildResponse(state);
    }
    
    // Check if log file was created
    std::this_thread::sleep_for(100ms); // Give time for log to be written
    bool logExists = std::filesystem::exists("test_access.log");
    TestFramework::assert_true(logExists, "Access log file should be created");
    
    if (logExists) {
        File logFile("test_access.log", "r");
        if (logFile.isOpen()) {
            auto logContent = logFile.readAll();
            std::string logStr(logContent.begin(), logContent.end());
            TestFramework::assert_contains(logStr, "GET", "Log should contain HTTP method");
            TestFramework::assert_contains(logStr, "/index.html", "Log should contain request path");
            logFile.close();
        }
    }
}

/// Main test runner
int main() {
    std::cout << "🧪 CustomFileServer Test Suite" << std::endl;
    std::cout << "=============================" << std::endl;
    
    try {
        setupTestEnvironment();
        
        testAuthentication();
        testFileServing();
        testDirectoryListing();
        testAccessControl();
        testSecurityFeatures();
        testErrorHandling();
        testMimeTypeDetection();
        testFilePathResolution();
        testLogging();
        
        cleanupTestEnvironment();
        
        TestFramework::printSummary();
        
        return TestFramework::failedTests > 0 ? 1 : 0;
        
    } catch (const std::exception& e) {
        std::cerr << "💥 Test suite failed with exception: " << e.what() << std::endl;
        cleanupTestEnvironment();
        return 1;
    }
}
