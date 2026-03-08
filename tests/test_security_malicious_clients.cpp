// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Security tests for malicious client attacks on HttpFileServer
// Tests all identified vulnerabilities to ensure they are properly mitigated

#include "HttpFileServer.h"
#include "FileIO.h"
#include "PathHelper.h"
#include "FileCache.h"
#include <string>
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

/// Test wrapper to expose protected methods for testing
class TestableHttpFileServer : public HttpFileServer {
    public:
    using HttpFileServer::HttpFileServer;

    // Expose buildResponse for testing
    void testBuildResponse(HttpClientState& state) { buildResponse(state); }
};

/// Test framework
class SecurityTestFramework {
    public:
    static void assert_true(bool condition, const std::string& message) {
        if (!condition) {
            fprintf(stderr, "\u274C SECURITY FAIL: %s\n", message.c_str());
            failedTests++;
        } else {
            printf("\u2705 SECURE: %s\n", message.c_str());
            passedTests++;
        }
    }

    static void assert_contains(const std::string& haystack,
        const std::string& needle, const std::string& message) {
        if (haystack.find(needle) == std::string::npos) {
            fprintf(stderr, "\u274C SECURITY FAIL: %s\n", message.c_str());
            fprintf(stderr, "   Expected to contain: '%s'\n", needle.c_str());
            failedTests++;
        } else {
            printf("\u2705 SECURE: %s\n", message.c_str());
            passedTests++;
        }
    }

    static void assert_not_contains(const std::string& haystack,
        const std::string& needle, const std::string& message) {
        if (haystack.find(needle) != std::string::npos) {
            fprintf(stderr, "\u274C SECURITY FAIL: %s\n", message.c_str());
            fprintf(stderr, "   Should NOT contain: '%s'\n", needle.c_str());
            failedTests++;
        } else {
            printf("\u2705 SECURE: %s\n", message.c_str());
            passedTests++;
        }
    }

    static void printSummary() {
        printf("\n=== SECURITY TEST SUMMARY ===\n");
        printf("Passed: %d\n", passedTests);
        printf("Failed: %d\n", failedTests);
        printf("Total:  %d\n", (passedTests + failedTests));

        if (failedTests == 0) {
            printf(
                "\U0001F512 ALL SECURITY TESTS PASSED - SYSTEM IS SECURE!\n");
        } else {
            printf("\U0001F6A8 SECURITY VULNERABILITIES DETECTED!\n");
        }
    }

    static int passedTests;
    static int failedTests;
};

int SecurityTestFramework::passedTests = 0;
int SecurityTestFramework::failedTests = 0;

/// Helper to make HTTP requests
std::string makeRequest(const std::string& method, const std::string& path) {
    StringBuilder request;
    request.append(method);
    request.append(" ");
    request.append(path);
    request.append(" HTTP/1.1\r\nHost: localhost\r\n\r\n");
    return request.toString();
}

/// Extract status line from response
std::string extractStatus(const std::string& response) {
    size_t end = response.find("\r\n");
    if (end != std::string::npos) {
        return response.substr(0, end);
    }
    return response;
}

/// Extract body from response
std::string extractBody(const std::string& response) {
    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        return response.substr(headerEnd + 4);
    }
    return "";
}

/// Extract header value
std::string extractHeader(
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

/// Setup test environment
void setupTestEnvironment() {
#ifdef _WIN32
    _mkdir("test_secure");
    _mkdir("test_secure\\public");
    _mkdir("test_secure\\secret");
    _mkdir("test_secure\\xss");
#else
    mkdir("test_secure", 0755);
    mkdir("test_secure/public", 0755);
    mkdir("test_secure/secret", 0755);
    mkdir("test_secure/xss", 0755);
#endif

    // Public files
    File publicFile("test_secure/public/index.html", "w");
    publicFile.writeString("<html><body>Public content</body></html>");
    publicFile.close();

    // XSS test directory: no index.html so directory listing is always shown.
    // Uses & in the filename — valid on all filesystems — to verify HTML
    // escaping is applied to all special chars, not just angle brackets.
    File xssFile("test_secure/xss/test&xss.html", "w");
    xssFile.writeString("<html><body>XSS test file</body></html>");
    xssFile.close();

    // Secret files (should not be accessible via path traversal)
    File secretFile("test_secure/secret/passwords.txt", "w");
    secretFile.writeString("admin:supersecret123");
    secretFile.close();

    // Large file for cache DoS testing
    File largeFile("test_secure/public/large.bin", "wb");
    std::vector<char> largeData(10 * 1024 * 1024, 'X'); // 10MB
    largeFile.write(largeData.data(), 1, largeData.size());
    largeFile.close();
}

/// Cleanup test environment
void cleanupTestEnvironment() {
    std::remove("test_secure/public/index.html");
    std::remove("test_secure/public/large.bin");
    std::remove("test_secure/secret/passwords.txt");
    std::remove("test_secure/xss/test&xss.html");
#ifdef _WIN32
    _rmdir("test_secure\\public");
    _rmdir("test_secure\\secret");
    _rmdir("test_secure\\xss");
    _rmdir("test_secure");
#else
    rmdir("test_secure/public");
    rmdir("test_secure/secret");
    rmdir("test_secure/xss");
    rmdir("test_secure");
#endif
}

/// TEST 1: Path Traversal Attacks
void testPathTraversalAttacks() {
    printf("\n=== TEST 1: PATH TRAVERSAL ATTACKS ===\n");

    HttpFileServer::Config config;
    config.documentRoot = "test_secure/public";
    TestableHttpFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Attack 1: Basic dot-dot-slash
    {
        std::string request = makeRequest("GET", "/../secret/passwords.txt");
        HttpClientState state;
        state.request = request;

        server.testBuildResponse(state);
        std::string status = extractStatus(state.responseBuf);
        std::string body = extractBody(state.responseBuf);

        SecurityTestFramework::assert_true(
            status.find("403") != std::string::npos
                || status.find("400") != std::string::npos,
            "Basic path traversal (/../) should be blocked");
        SecurityTestFramework::assert_not_contains(body, "supersecret123",
            "Secret data should not be leaked via path traversal");
    }

    // Attack 2: URL-encoded dot-dot-slash (%2e%2e%2f)
    {
        std::string request
            = makeRequest("GET", "/%2e%2e/secret/passwords.txt");
        HttpClientState state;
        state.request = request;

        server.testBuildResponse(state);
        std::string status = extractStatus(state.responseBuf);
        std::string body = extractBody(state.responseBuf);

        SecurityTestFramework::assert_true(
            status.find("403") != std::string::npos
                || status.find("400") != std::string::npos,
            "URL-encoded path traversal (%2e%2e/) should be blocked");
        SecurityTestFramework::assert_not_contains(body, "supersecret123",
            "Secret data should not be leaked via URL-encoded traversal");
    }

    // Attack 3: Double URL-encoded (%252e%252e%252f)
    {
        std::string request
            = makeRequest("GET", "/%252e%252e/secret/passwords.txt");
        HttpClientState state;
        state.request = request;

        server.testBuildResponse(state);
        std::string status = extractStatus(state.responseBuf);
        std::string body = extractBody(state.responseBuf);

        SecurityTestFramework::assert_true(
            status.find("403") != std::string::npos
                || status.find("400") != std::string::npos
                || status.find("404") != std::string::npos,
            "Double URL-encoded path traversal should be blocked");
        SecurityTestFramework::assert_not_contains(body, "supersecret123",
            "Secret data should not be leaked via double encoding");
    }

    // Attack 4: Windows backslash traversal
    {
        std::string request = makeRequest("GET", "/..\\secret\\passwords.txt");
        HttpClientState state;
        state.request = request;

        server.testBuildResponse(state);
        std::string status = extractStatus(state.responseBuf);
        std::string body = extractBody(state.responseBuf);

        SecurityTestFramework::assert_true(
            status.find("403") != std::string::npos
                || status.find("400") != std::string::npos
                || status.find("404") != std::string::npos,
            "Windows backslash path traversal should be blocked");
        SecurityTestFramework::assert_not_contains(body, "supersecret123",
            "Secret data should not be leaked via backslash traversal");
    }

    // Attack 5: Absolute path
    {
        std::string request = makeRequest("GET", "/etc/passwd");
        HttpClientState state;
        state.request = request;

        server.testBuildResponse(state);
        std::string status = extractStatus(state.responseBuf);

        SecurityTestFramework::assert_true(
            status.find("403") != std::string::npos
                || status.find("400") != std::string::npos
                || status.find("404") != std::string::npos,
            "Absolute path access should be blocked");
    }
}

/// TEST 2: XSS (Cross-Site Scripting) Attacks
void testXSSAttacks() {
    printf("\n=== TEST 2: XSS ATTACKS ===\n");

    HttpFileServer::Config config;
    config.documentRoot = "test_secure/xss"; // no index.html → always lists dir
    config.enableDirectoryListing = true;
    TestableHttpFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Attack 1: XSS in directory listing via malicious filename
    // Uses '&' in filename (valid on all filesystems, needs HTML escaping)
    {
        std::string request = makeRequest("GET", "/");
        HttpClientState state;
        state.request = request;

        server.testBuildResponse(state);
        std::string body = extractBody(state.responseBuf);

        // The filename contains &  — it must be HTML-escaped as &amp;
        SecurityTestFramework::assert_not_contains(body, "test&xss",
            "Raw & in filenames should be HTML-escaped in directory listing");
        SecurityTestFramework::assert_contains(body, "test&amp;xss",
            "& in filename should be escaped as &amp; in directory listing");
    }

    // Attack 2: XSS in error messages
    {
        std::string request
            = makeRequest("GET", "/<script>alert(document.cookie)</script>");
        HttpClientState state;
        state.request = request;

        server.testBuildResponse(state);
        std::string body = extractBody(state.responseBuf);

        SecurityTestFramework::assert_not_contains(body,
            "<script>alert(document.cookie)</script>",
            "Script tags in error messages should be escaped");
    }
}

/// TEST 3: Security Headers
void testSecurityHeaders() {
    printf("\n=== TEST 3: SECURITY HEADERS ===\n");

    HttpFileServer::Config config;
    config.documentRoot = "test_secure/public";
    config.enableSecurityHeaders = true;
    TestableHttpFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    std::string request = makeRequest("GET", "/index.html");
    HttpClientState state;
    state.request = request;

    server.testBuildResponse(state);
    std::string response = state.responseBuf;

    SecurityTestFramework::assert_contains(response,
        "X-Content-Type-Options: nosniff",
        "Should include X-Content-Type-Options header");
    SecurityTestFramework::assert_contains(response, "X-Frame-Options: DENY",
        "Should include X-Frame-Options header");
    SecurityTestFramework::assert_contains(response, "Content-Security-Policy:",
        "Should include Content-Security-Policy header");
    SecurityTestFramework::assert_contains(response,
        "Referrer-Policy: no-referrer",
        "Should include Referrer-Policy header");
}

/// TEST 4: Information Leakage
void testInformationLeakage() {
    printf("\n=== TEST 4: INFORMATION LEAKAGE ===\n");

    HttpFileServer::Config config;
    config.documentRoot = "test_secure/public";
    config.hideServerVersion = true;
    TestableHttpFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Test error page doesn't leak server version
    {
        std::string request = makeRequest("GET", "/nonexistent.html");
        HttpClientState state;
        state.request = request;

        server.testBuildResponse(state);
        std::string body = extractBody(state.responseBuf);

        SecurityTestFramework::assert_not_contains(body,
            "aiSocks HttpFileServer",
            "Server version should be hidden in error pages when configured");
    }

    // Test directory listing doesn't leak full paths
    {
        config.enableDirectoryListing = true;
        TestableHttpFileServer server2(
            ServerBind{"127.0.0.1", Port{0}}, config);

        std::string request = makeRequest("GET", "/");
        HttpClientState state;
        state.request = request;

        server2.testBuildResponse(state);
        std::string body = extractBody(state.responseBuf);

        SecurityTestFramework::assert_not_contains(body, "test_secure/public",
            "Full directory paths should not be exposed in listings");
    }
}

/// TEST 5: Cache DoS Protection
void testCacheDoSProtection() {
    printf("\n=== TEST 5: CACHE DOS PROTECTION ===\n");

    // Test FileCache limits
    {
        FileCache::Config cacheConfig;
        cacheConfig.maxEntries = 5;
        cacheConfig.maxTotalBytes = 1024 * 1024; // 1MB
        cacheConfig.maxFileSize = 512 * 1024; // 512KB

        FileCache cache(cacheConfig);

        // Try to cache a file that's too large
        std::vector<char> largeContent(1024 * 1024, 'X'); // 1MB file
        cache.put("large.bin", largeContent, 12345);

        SecurityTestFramework::assert_true(cache.size() == 0,
            "Files larger than maxFileSize should not be cached");

        // Fill cache with small files
        for (int i = 0; i < 10; i++) {
            std::vector<char> content(100, static_cast<char>('A' + i));
            cache.put("file" + std::to_string(i) + ".txt", content, 12345 + i);
        }

        SecurityTestFramework::assert_true(
            cache.size() <= cacheConfig.maxEntries,
            "Cache should not exceed maxEntries limit");
        SecurityTestFramework::assert_true(
            cache.totalBytes() <= cacheConfig.maxTotalBytes,
            "Cache should not exceed maxTotalBytes limit");
    }

    // Test LRU eviction
    {
        FileCache::Config cacheConfig;
        cacheConfig.maxEntries = 3;
        FileCache cache(cacheConfig);

        std::vector<char> content(100, 'X');
        cache.put("file1.txt", content, 1);
        cache.put("file2.txt", content, 2);
        cache.put("file3.txt", content, 3);

        // Access file1 to make it most recently used
        cache.get("file1.txt", 1);

        // Add file4, should evict file2 (least recently used)
        cache.put("file4.txt", content, 4);

        SecurityTestFramework::assert_true(
            cache.size() == 3, "LRU eviction should maintain cache size limit");
        SecurityTestFramework::assert_true(cache.get("file1.txt", 1) != nullptr,
            "Most recently used file should remain in cache");
        SecurityTestFramework::assert_true(cache.get("file2.txt", 2) == nullptr,
            "Least recently used file should be evicted");
    }
}

/// TEST 6: URL Decoding Issues
void testURLDecodingIssues() {
    printf("\n=== TEST 6: URL DECODING ISSUES ===\n");

    HttpFileServer::Config config;
    config.documentRoot = "test_secure/public";
    TestableHttpFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Test that + is NOT treated as space in paths (RFC 3986)
    {
        // Create a file with + in the name
        File plusFile("test_secure/public/file+name.txt", "w");
        plusFile.writeString("Content with plus in filename");
        plusFile.close();

        std::string request = makeRequest("GET", "/file+name.txt");
        HttpClientState state;
        state.request = request;

        server.testBuildResponse(state);
        std::string status = extractStatus(state.responseBuf);
        std::string body = extractBody(state.responseBuf);

        SecurityTestFramework::assert_true(
            status.find("200") != std::string::npos,
            "Plus sign in path should be treated literally, not as space");
        SecurityTestFramework::assert_contains(body, "Content with plus",
            "File with + in name should be accessible");

        std::remove("test_secure/public/file+name.txt");
    }
}

/// TEST 7: Method Validation
void testMethodValidation() {
    printf("\n=== TEST 7: HTTP METHOD VALIDATION ===\n");

    HttpFileServer::Config config;
    config.documentRoot = "test_secure/public";
    TestableHttpFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Test that only GET and HEAD are allowed
    const char* methods[]
        = {"POST", "PUT", "DELETE", "PATCH", "OPTIONS", "TRACE"};

    for (const char* method : methods) {
        StringBuilder request;
        request.append(method);
        request.append(" /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n");

        HttpClientState state;
        state.request = request.toString();

        server.testBuildResponse(state);
        std::string status = extractStatus(state.responseBuf);

        std::string testMsg
            = std::string("HTTP method ") + method + " should be rejected";
        SecurityTestFramework::assert_true(
            status.find("405") != std::string::npos, testMsg);
    }
}

/// TEST 8: File Type Restrictions
void testFileTypeRestrictions() {
    printf("\n=== TEST 8: FILE TYPE RESTRICTIONS ===\n");

    HttpFileServer::Config config;
    config.documentRoot = "test_secure/public";
    config.maxFileSize = 1024 * 1024; // 1MB max
    TestableHttpFileServer server(ServerBind{"127.0.0.1", Port{0}}, config);

    // Test that large files are rejected
    {
        std::string request = makeRequest("GET", "/large.bin");
        HttpClientState state;
        state.request = request;

        server.testBuildResponse(state);
        std::string status = extractStatus(state.responseBuf);

        SecurityTestFramework::assert_true(
            status.find("403") != std::string::npos,
            "Files exceeding maxFileSize should be rejected");
    }
}

/// Main test runner
int main() {
    printf("\U0001F512 HttpFileServer Security Test Suite\n");
    printf("======================================\n");
    printf("Testing protection against malicious client attacks\n\n");

    try {
        setupTestEnvironment();

        testPathTraversalAttacks();
        testXSSAttacks();
        testSecurityHeaders();
        testInformationLeakage();
        testCacheDoSProtection();
        testURLDecodingIssues();
        testMethodValidation();
        testFileTypeRestrictions();

        cleanupTestEnvironment();

        SecurityTestFramework::printSummary();

        return SecurityTestFramework::failedTests > 0 ? 1 : 0;

    } catch (const std::exception& e) {
        fprintf(stderr,
            "\U0001F4A5 Security test suite failed with exception: %s\n",
            e.what());
        cleanupTestEnvironment();
        return 1;
    }
}
