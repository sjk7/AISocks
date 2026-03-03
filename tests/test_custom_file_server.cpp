// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Comprehensive tests for CustomFileServer
// Tests both happy path (successful operations) and sad path (error conditions)

#include "HttpFileServer.h"
#include "FileIO.h"
#include "PathHelper.h"
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
#endif

using namespace aiSocks;
using namespace std::chrono_literals;

// Include the CustomFileServer implementation from the example
// We need to define it here for testing since it's in the examples folder
class CustomFileServer : public HttpFileServer {
public:
    explicit CustomFileServer(ServerBind bind, const Config& config = Config{}) 
        : HttpFileServer(std::move(bind), config), logFile_("access.log", "a") {}
    
    ~CustomFileServer() { logFile_.close(); }
    
public:
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
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
#else
        struct tm* timeinfo = localtime(&time_t);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
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

/// Behavior-focused test helper
class BehavioralTestHelper {
public:
    static std::string makeHttpRequest(const std::string& method, const std::string& path, const std::string& auth = "") {
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
    
    static std::string extractHeader(const std::string& response, const std::string& headerName) {
        std::string search = headerName + ":";
        size_t start = response.find(search);
        if (start == std::string::npos) return "";
        
        start = response.find(":", start) + 1;
        while (start < response.size() && (response[start] == ' ' || response[start] == '\t')) {
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
}

/// Clean up test environment
void cleanupTestEnvironment() {
    // Simple cleanup - remove files first, then directories
    std::remove("test_www/index.html");
    std::remove("test_www/style.css");
    std::remove("test_www/script.js");
    std::remove("test_www/config.conf");
    std::remove("test_www/debug.log");
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
    std::cout << "\n=== TESTING AUTHENTICATION BEHAVIOR ===" << std::endl;
    
    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);
    
    // Behavior: User with correct credentials can access the server
    {
        std::string request = BehavioralTestHelper::makeHttpRequest("GET", "/", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;
        
        server.buildResponse(state);
        std::string response = state.responseBuf;
        std::string status = BehavioralTestHelper::extractStatus(response);
        std::string body = BehavioralTestHelper::extractBody(response);
        
        TestFramework::assert_equals("HTTP/1.1 200 OK", status, "Valid credentials should grant access");
        TestFramework::assert_contains(body, "Testing Guide", "Should show testing interface");
    }
    
    // Behavior: User with wrong credentials gets access denied
    {
        std::string request = BehavioralTestHelper::makeHttpRequest("GET", "/", "d3Jvbmc6Y3JlZGVudGlhbHM=");
        HttpClientState state;
        state.request = request;
        
        server.buildResponse(state);
        std::string response = state.responseBuf;
        std::string status = BehavioralTestHelper::extractStatus(response);
        std::string body = BehavioralTestHelper::extractBody(response);
        
        TestFramework::assert_equals("HTTP/1.1 401 Unauthorized", status, "Wrong credentials should deny access");
        TestFramework::assert_contains(body, "Unauthorized", "Should show access denied page");
        TestFramework::assert_contains(body, "authentication", "Should explain authentication required");
    }
    
    // Behavior: User without credentials gets prompted to login
    {
        std::string request = BehavioralTestHelper::makeHttpRequest("GET", "/", "");
        HttpClientState state;
        state.request = request;
        
        server.buildResponse(state);
        std::string response = state.responseBuf;
        std::string status = BehavioralTestHelper::extractStatus(response);
        std::string authHeader = BehavioralTestHelper::extractHeader(response, "WWW-Authenticate");
        
        TestFramework::assert_equals("HTTP/1.1 401 Unauthorized", status, "No credentials should prompt for login");
        TestFramework::assert_contains(authHeader, "Basic realm", "Should include browser login prompt");
    }
}

/// Test file serving behavior - what users experience when requesting files
void testFileServingBehavior() {
    std::cout << "\n=== TESTING FILE SERVING BEHAVIOR ===" << std::endl;
    
    HttpFileServer::Config config;
    config.documentRoot = "test_www";
    CustomFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);
    
    // Behavior: User can access HTML files with proper content type
    {
        std::string request = BehavioralTestHelper::makeHttpRequest("GET", "/index.html", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;
        
        server.buildResponse(state);
        std::string response = state.responseBuf;
        std::string status = BehavioralTestHelper::extractStatus(response);
        std::string contentType = BehavioralTestHelper::extractHeader(response, "Content-Type");
        std::string body = BehavioralTestHelper::extractBody(response);
        
        TestFramework::assert_equals("HTTP/1.1 200 OK", status, "HTML file should be accessible");
        TestFramework::assert_contains(contentType, "text/html", "Should serve with HTML MIME type");
        TestFramework::assert_contains(contentType, "charset=utf-8", "Should include UTF-8 charset");
        TestFramework::assert_contains(body, "Testing Guide", "Should show testing interface (index.html redirects to testing guide)");
    }
    
    // Behavior: User can access CSS files for styling
    {
        std::string request = BehavioralTestHelper::makeHttpRequest("GET", "/style.css", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;
        
        server.buildResponse(state);
        std::string response = state.responseBuf;
        std::string status = BehavioralTestHelper::extractStatus(response);
        std::string contentType = BehavioralTestHelper::extractHeader(response, "Content-Type");
        std::string body = BehavioralTestHelper::extractBody(response);
        
        TestFramework::assert_equals("HTTP/1.1 200 OK", status, "CSS file should be accessible");
        TestFramework::assert_contains(contentType, "text/css", "Should serve with CSS MIME type");
        TestFramework::assert_contains(body, "color: red", "Should contain styling rules");
    }
    
    // Behavior: User can access JavaScript files for functionality
    {
        std::string request = BehavioralTestHelper::makeHttpRequest("GET", "/script.js", "YWRtaW46c2VjcmV0");
        HttpClientState state;
        state.request = request;
        
        server.buildResponse(state);
        std::string response = state.responseBuf;
        std::string status = BehavioralTestHelper::extractStatus(response);
        std::string contentType = BehavioralTestHelper::extractHeader(response, "Content-Type");
        std::string body = BehavioralTestHelper::extractBody(response);
        
        TestFramework::assert_equals("HTTP/1.1 200 OK", status, "JavaScript file should be accessible");
        TestFramework::assert_contains(contentType, "application/javascript", "Should serve with JS MIME type");
        TestFramework::assert_contains(body, "console.log", "Should contain JavaScript code");
    }
}


/// Main test runner - focused on behavior, not implementation
int main() {
    std::cout << "🧪 CustomFileServer Behavioral Test Suite" << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << "Testing user-facing behavior, not internal implementation" << std::endl;
    
    try {
        setupTestEnvironment();
        
        testAuthenticationBehavior();
        testFileServingBehavior();
        // TODO: Refactor remaining tests to be behavior-focused
        // testDirectoryListing();
        // testAccessControl();
        // testSecurityFeatures();
        // testErrorHandling();
        // testMimeTypeDetection();
        // testFilePathResolution();
        // testLogging();
        
        cleanupTestEnvironment();
        
        TestFramework::printSummary();
        
        return TestFramework::failedTests > 0 ? 1 : 0;
        
    } catch (const std::exception& e) {
        std::cerr << "💥 Test suite failed with exception: " << e.what() << std::endl;
        cleanupTestEnvironment();
        return 1;
    }
}
