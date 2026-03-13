// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

//
// LEVEL 3: ADVANCED FILE SERVER (Extending HttpFileServer)
//
// This example demonstrates inheritance and customization of HttpFileServer.
// By deriving from HttpFileServer and overriding virtual methods, you can:
//   - Add authentication (onAuthenticate)
//   - Add custom logging (onResponseSent)
//   - Customize error pages (generateErrorHtml)
//   - Add custom headers, validation, etc.
//
// ✓ Use this approach when you need to extend file server behavior
// ✓ Shows the power of the virtual method hooks
// ✓ Production example with authentication, access logs, and styled error pages
//
// For simpler examples, see:
//   → simple_file_server.cpp    (basic HttpFileServer usage, ~50 lines)
//   → low_level_http_server.cpp (manual HTTP response building)
//

// Example: Advanced HTTP file server with authentication and logging
// Demonstrates how to derive from HttpFileServer and override virtual functions

#include "HttpFileServer.h"
#include "advanced_file_server_strings.h"
#include "advanced_file_server_pages.h"
#include "CustomFileServerHtmlHelpers.h"
#include "FileIO.h"
#include <cstdio>
#include <chrono>
#include <vector>

using namespace aiSocks;

// ============================================================================
// CustomFileServer - Extends HttpFileServer with authentication and logging
// ============================================================================
//
// This class demonstrates the extensibility of HttpFileServer:
//   • Adds Basic Authentication (username/password)
//   • Logs all requests to access.log file
//   • Provides custom styled error pages (404, 401, 403)
//
// Key virtual methods overridden:
//   • onAuthenticate()   - Validates Basic Auth credentials
//   • onResponseSent()   - Logs completed requests to file
//   • generateErrorHtml() - Creates styled HTML error pages
//
// See simple_file_server.cpp for basic HttpFileServer usage without
// customization. See low_level_http_server.cpp for manual HTTP response
// building.
//
class CustomFileServer : public HttpFileServer {
    public:
    explicit CustomFileServer(const ServerBind& bind, const Config& config,
        Result<TcpSocket>* result = nullptr)
        : HttpFileServer(bind, config, result) {

        // Open log file
        logFile_.open("access.log", "a");

        // Note: Authentication header will be added by the base class
    }

    ~CustomFileServer() { logFile_.close(); }

    protected:
    /// Override to add access logging
    void buildResponse(HttpClientState& state) override {
        // Parse request first
        auto request = HttpRequest::parse(state.request);

        // Log the request
        if (request.valid) {
            logRequest(request, state);
        }

        // Root redirects to the public landing page (no auth required)
        if (request.path == "/") {
            state.responseBuf = "HTTP/1.1 302 Found\r\nLocation: "
                                "/public.html\r\nContent-Length: 0\r\n\r\n";
            state.responseView = state.responseBuf;
            return;
        }

        // Allow a small set of paths without authentication
        if (isPublicPath(request.path)) {
            HttpFileServer::buildResponse(state);
            return;
        }

        // Check authentication
        if (!isAuthenticated(request)) {
            sendAuthRequired(state);
            return;
        }

        // Special handling for large test file - generate on-the-fly
        if (request.path == "/large500MB.bin") {
            generateLargeFile(state, request);
            return;
        }

        if (request.path == "/index.html") {
            const std::string instructions
                = AdvancedFileServerPages::generateTestingInstructions();
            std::string response;
            response.reserve(256 + instructions.size());
            response.append("HTTP/1.1 200 OK\r\n");
            response.append(
                "Content-Type: text/html; charset=utf-8\r\nContent-Length: ");
            response += std::to_string(instructions.size());
            response.append("\r\n");

            for (const auto& [name, value] : getConfig().customHeaders) {
                response.append(name);
                response.append(": ");
                response.append(value);
                response.append("\r\n");
            }

            response.append("\r\n");
            if (request.method != "HEAD") {
                response.append(instructions);
            }

            state.responseBuf = std::move(response);
            state.responseView = state.responseBuf;
            return;
        }

        if (request.path == "/jstest.html") {
            const std::string demoPage
                = AdvancedFileServerPages::generateDemoPage();
            std::string response;
            response.reserve(256 + demoPage.size());
            response.append("HTTP/1.1 200 OK\r\n");
            response.append(
                "Content-Type: text/html; charset=utf-8\r\nContent-Length: ");
            response += std::to_string(demoPage.size());
            response.append("\r\n");

            for (const auto& [name, value] : getConfig().customHeaders) {
                response.append(name);
                response.append(": ");
                response.append(value);
                response.append("\r\n");
            }

            response.append("\r\n");
            if (request.method != "HEAD") {
                response.append(demoPage);
            }

            state.responseBuf = std::move(response);
            state.responseView = state.responseBuf;
            return;
        }

        if (request.path == "/access.log") {
            if (!isLocalClient(state.peerAddress)) {
                sendError(state, 403, "Forbidden",
                    "The access log viewer is only available from local "
                    "connections.");
                return;
            }

            const std::string page
                = AdvancedFileServerPages::generateAccessLogTailPage(
                    readAccessLogTail(100));
            std::string response;
            response.reserve(256 + page.size());
            response.append("HTTP/1.1 200 OK\r\n");
            response.append(
                "Content-Type: text/html; charset=utf-8\r\nContent-Length: ");
            response += std::to_string(page.size());
            response.append("\r\nCache-Control: no-store\r\nRefresh: 2\r\n");

            for (const auto& [name, value] : getConfig().customHeaders) {
                response.append(name);
                response.append(": ");
                response.append(value);
                response.append("\r\n");
            }

            response.append("\r\n");
            if (request.method != "HEAD") {
                response.append(page);
            }

            state.responseBuf = std::move(response);
            state.responseView = state.responseBuf;
            return;
        }

        // Call base implementation for all other paths
        HttpFileServer::buildResponse(state);
    }

    /// Override to allow large test file to bypass size limit
    bool isFileSizeAcceptable(
        const std::string& filePath, size_t fileSize) const override {
        // Allow large500MB.bin to bypass the size limit for testing
        if (filePath.find("large500MB.bin") != std::string::npos) {
            return true;
        }

        // Use base implementation for all other files
        return HttpFileServer::isFileSizeAcceptable(filePath, fileSize);
    }

    /// Override to add access control
    bool isAccessAllowed(
        const std::string& filePath, const FileInfo& fileInfo) const override {
        // Call base implementation first
        if (!HttpFileServer::isAccessAllowed(filePath, fileInfo)) {
            return false;
        }

        // Additional custom checks
        std::string ext = getFileExtension(filePath);

        // Deny access to sensitive files
        if (ext == ".conf" || ext == ".log" || ext == ".tmp") {
            return false;
        }

        // Allow access to everything else
        return true;
    }

    /// Override to customize error pages
    std::string generateErrorHtml(int code, const std::string& status,
        const std::string& message) const override {
        return AdvancedFileServerPages::generateErrorHtml(
            code, status, message);
    }

    /// Override to customize directory request handling
    void handleDirectoryRequest(HttpClientState& state,
        const std::string& dirPath, const HttpRequest& request) override {
        // For all directories, show normal directory listing
        HttpFileServer::handleDirectoryRequest(state, dirPath, request);
    }

    /// Generate 500MB test file on-the-fly
    void generateLargeFile(HttpClientState& state, const HttpRequest& request) {
        const size_t fileSize = 500 * 1024 * 1024; // 500 MB

        std::string response;
        response.reserve(256);
        response.append("HTTP/1.1 200 OK\r\n");
        response.append("Content-Type: application/octet-stream\r\n");
        response.append(
            "Content-Disposition: attachment; filename=\"large500MB.bin\"\r\n");
        response += "Content-Length: ";
        response += std::to_string(fileSize);
        response += "\r\n";

        // Add custom headers
        for (const auto& [name, value] : getConfig().customHeaders) {
            response.append(name);
            response.append(": ");
            response.append(value);
            response.append("\r\n");
        }

        response.append("\r\n");

        // For HEAD requests, just send headers
        if (request.method == "HEAD") {
            state.responseBuf = response;
            state.responseView = state.responseBuf;
            return;
        }

        // For GET requests, generate the file content
        std::string headers = std::move(response);
        state.responseBuf.reserve(headers.size() + fileSize);
        state.responseBuf = headers;

        // Generate 500MB of repeating pattern data
        const char pattern[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        const size_t patternLen = sizeof(pattern) - 1;

        size_t remaining = fileSize;
        while (remaining > 0) {
            size_t chunkSize
                = (remaining < patternLen) ? remaining : patternLen;
            state.responseBuf.append(pattern, chunkSize);
            remaining -= chunkSize;
        }

        state.responseView = state.responseBuf;
    }

    /// Override to customize directory listing
    std::string generateDirectoryListing(
        const std::string& dirPath) const override {
        return AdvancedFileServerPages::generateDirectoryListing(
            dirPath, getConfig().documentRoot);
    }

    private:
    File logFile_;

bool isLocalClient(const std::string& peerAddress) const {
    return peerAddress == "127.0.0.1" || peerAddress == "::1"
        || peerAddress == "::ffff:127.0.0.1" || peerAddress == "localhost";
}

std::string readAccessLogTail(size_t maxLines) const {
    std::FILE* file = std::fopen("access.log", "rb");
    if (!file) return {};

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return {};
    }
    const long fileSize = std::ftell(file);
    if (fileSize <= 0) {
        std::fclose(file);
        return {};
    }
    if (std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return {};
    }

    std::vector<char> bytes(static_cast<size_t>(fileSize));
    const size_t bytesRead = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    if (bytesRead == 0) return {};
    bytes.resize(bytesRead);

    std::string content(bytes.begin(), bytes.end());
    std::vector<size_t> lineStarts;
    lineStarts.reserve(64);
    lineStarts.push_back(0);

    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n' && i + 1 < content.size()) {
            lineStarts.push_back(i + 1);
        }
    }

    const size_t startIndex
        = (lineStarts.size() > maxLines) ? (lineStarts.size() - maxLines) : 0;
    return content.substr(lineStarts[startIndex]);
}

void logRequest(const HttpRequest& request, const HttpClientState& state) {
    (void)state; // Suppress unused parameter warning - available for future
                 // enhancements
    if (!logFile_.isOpen()) return;

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

#ifdef _WIN32
    struct tm timeinfo = {};
    localtime_s(&timeinfo, &time_t);
    logFile_.printf("%04d-%02d-%02d %02d:%02d:%02d %s %s from client\n",
        static_cast<int>(timeinfo.tm_year + 1900),
        static_cast<int>(timeinfo.tm_mon + 1),
        static_cast<int>(timeinfo.tm_mday), static_cast<int>(timeinfo.tm_hour),
        static_cast<int>(timeinfo.tm_min), static_cast<int>(timeinfo.tm_sec),
        request.method.c_str(), request.path.c_str());
#else
    struct tm* timeinfo = localtime(&time_t);
    logFile_.printf(
        "%04d-%02d-%02d %02d:%02d:%02d %.*s %s from client\n", //-V111
                                                               ////-V111
        static_cast<int>(timeinfo->tm_year + 1900),
        static_cast<int>(timeinfo->tm_mon + 1),
        static_cast<int>(timeinfo->tm_mday),
        static_cast<int>(timeinfo->tm_hour), static_cast<int>(timeinfo->tm_min),
        static_cast<int>(timeinfo->tm_sec),
        static_cast<int>(request.method.size()), request.method.data(),
        request.path.c_str());
#endif
    logFile_.flush();
}

/// Returns true for paths that are served without authentication.
bool isPublicPath(const std::string& path) const {
    static const std::string publicPaths[] = {
        "/public.html",
    };
    for (const auto& p : publicPaths) {
        if (path == p) return true;
    }
    return false;
}

bool isAuthenticated(const HttpRequest& request) const {
    // Simple basic authentication (username: admin, password: secret)
    auto authIt = request.headers.find("authorization");
    if (authIt == request.headers.end()) {
        return false;
    }

    const std::string_view auth = authIt->second;
    if (auth.substr(0, 6) != "Basic ") {
        return false;
    }

    // In a real implementation, you'd decode the Base64 credentials
    // For this example, we'll just check for a specific token
    return auth.substr(6) == "YWRtaW46c2VjcmV0"; // "admin:secret" in Base64
}

void sendAuthRequired(HttpClientState& state) {
    std::string htmlBody = generateErrorHtml(401, "Unauthorized",
        "This server requires authentication. Please provide valid "
        "credentials.");

    std::string response;
    response.reserve(256 + htmlBody.size());
    response.append("HTTP/1.1 401 Unauthorized\r\n");
    response.append("Content-Type: text/html\r\nContent-Length: ");
    response += std::to_string(htmlBody.size());
    response.append(
        "\r\nWWW-Authenticate: Basic realm=\"Secure Area\"\r\n\r\n");
    response.append(htmlBody);

    state.responseBuf = std::move(response);
    state.responseView = state.responseBuf;
}

std::string getCurrentTime() const {
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

    return std::string(buffer);
}

std::string getFileExtension(const std::string& filePath) const {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos != std::string::npos && dotPos < filePath.length() - 1) {
        return filePath.substr(dotPos);
    }
    return "";
}
}
;
int main(int argc, char* argv[]) {

    printf("%s", ServerStrings::HEADER);

    // Print build information
    printf("%s", ServerStrings::BUILD_INFO_PREFIX);
#if defined(__linux__)
    printf("Linux");
#elif defined(__APPLE__)
    printf("macOS");
#elif defined(_WIN32)
    printf("Windows");
#else
    printf("Unknown OS");
#endif

#if defined(CMAKE_BUILD_TYPE)
    printf(" (%s)", CMAKE_BUILD_TYPE);
#elif defined(NDEBUG)
    printf(" (Release)");
#else
    printf(" (Debug)");
#endif

    printf(", built %s %s\n", __DATE__, __TIME__);

    // Print max clients limit
    printf("%s%zu\n", ServerStrings::MAX_CLIENTS_PREFIX,
        static_cast<size_t>(ClientLimit::Default));

    uint16_t port = 8080;
    std::string root = "../www";

    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }
    if (argc > 2) {
        root = argv[2];
    }

    // Configure the file server
    HttpFileServer::Config config;
    config.documentRoot = root;
    config.indexFile = "index.html";
    config.enableDirectoryListing = true;
    config.enableETag = true;
    config.enableLastModified = true;
    config.maxFileSize = 50 * 1024 * 1024;

    // Add custom headers
    config.customHeaders["Server"] = "Custom-FileServer/1.0";
    config.customHeaders["X-Content-Type-Options"] = "nosniff";
    config.customHeaders["X-Frame-Options"] = "DENY";

    // Create and start the custom server with detailed error information
    Result<TcpSocket> serverResult
        = Result<TcpSocket>::failure(SocketError::Unknown, "initial");
    CustomFileServer server(
        ServerBind{"0.0.0.0", Port{port}}, config, &serverResult);

    // Check if server creation succeeded (bind/listen)
    if (!server.isValid()) {
        fprintf(stderr, "ERROR: Server failed to start: %s\n",
            serverResult.message().c_str());
        fprintf(
            stderr, "Error code: %d\n", static_cast<int>(serverResult.error()));
        printf("%s", ServerStrings::SERVER_STOPPED);
        printf("%s", ServerStrings::LOG_SAVED);
        printf("%s", ServerStrings::THANK_YOU);
        return 1;
    }

    printf(ServerStrings::STARTING, port);
    printf("%s%s\n", ServerStrings::SERVING_FROM, config.documentRoot.c_str());
    printf(ServerStrings::PUBLIC_PAGE, port);

    server.run();

    printf("%s", ServerStrings::SERVER_STOPPED);
    printf("%s", ServerStrings::LOG_SAVED);
    printf("%s", ServerStrings::THANK_YOU);

    return 0;
}
