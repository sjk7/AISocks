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
#include "PathHelper.h"
#include "advanced_file_server_strings.h"
#include "CustomFileServerHtmlHelpers.h"
#include "FileIO.h"
#include <cstdio>
#include <chrono>

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

        // Special handling for root path - show testing instructions
        if (request.path == "/") {
            std::string instructions = generateTestingInstructions();

            StringBuilder response(
                256 + instructions.size()); // Reserve for headers + body
            response.append("HTTP/1.1 200 OK\r\n");
            response.append(
                "Content-Type: text/html; charset=utf-8\r\nContent-Length: ");
            response.appendFormat("%zu", instructions.size());
            response.append("\r\n");

            // Add custom headers
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

        // Call base implementation for all other paths
        HttpFileServer::buildResponse(state);
    }

    /// Override to add custom MIME types
    std::string getMimeType(const std::string& filePath) const override {
        std::string ext = getFileExtension(filePath);

        // Add custom MIME types
        if (ext == ".wasm") return "application/wasm";
        if (ext == ".ts") return "application/typescript";
        if (ext == ".jsx") return "text/jsx";
        if (ext == ".tsx") return "text/tsx";

        // Fall back to base implementation
        return HttpFileServer::getMimeType(filePath);
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
        StringBuilder html(1024); // Reserve for custom error page HTML
        html.append("<!DOCTYPE html>\n");
        html.append("<html><head><title>");
        html.appendFormat("%d", code);
        html.append(" ");
        html.append(HtmlEscape::encode(status));
        html.append("</title>\n");
        html.append("<style>\n");
        html.append("body { font-family: Arial, sans-serif; margin: 40px; "
                    "background-color: #f5f5f5; }\n");
        html.append(".error-container { max-width: 600px; margin: 0 auto; "
                    "background: white; padding: 30px; border-radius: 8px; "
                    "box-shadow: 0 2px 10px rgba(0,0,0,0.1); }\n");
        html.append("h1 { color: #e74c3c; margin-bottom: 20px; }\n");
        html.append("p { color: #555; line-height: 1.6; }\n");
        html.append(".back-link { display: inline-block; margin-top: 20px; "
                    "padding: 10px 20px; background: #3498db; color: white; "
                    "text-decoration: none; border-radius: 4px; }\n");
        html.append(".back-link:hover { background: #2980b9; }\n");
        html.append("</style></head>\n");
        html.append("<body><div class=\"error-container\">\n");
        html.append("<h1>");
        html.appendFormat("%d", code);
        html.append(" ");
        html.append(HtmlEscape::encode(status));
        html.append("</h1>\n");
        html.append("<p>");
        html.append(HtmlEscape::encode(message));
        html.append("</p>\n");
        html.append("<a href=\"/\" class=\"back-link\">← Back to Home</a>\n");
        html.append("</div></body></html>");
        return html.toString();
    }

    /// Override to show testing instructions as default page
    void handleDirectoryRequest(HttpClientState& state,
        const std::string& dirPath, const HttpRequest& request) override {
        // If this is the root directory, show testing instructions instead of
        // directory listing
        if (dirPath == getConfig().documentRoot) {
            std::string instructions = generateTestingInstructions();

            StringBuilder response(
                256 + instructions.size()); // Reserve for headers + body
            response.append("HTTP/1.1 200 OK\r\n");
            response.append(
                "Content-Type: text/html; charset=utf-8\r\nContent-Length: ");
            response.appendFormat("%zu", instructions.size());
            response.append("\r\n");

            // Add custom headers
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

        // For subdirectories, show normal directory listing
        HttpFileServer::handleDirectoryRequest(state, dirPath, request);
    }

    std::string generateTestingInstructions() const {
        StringBuilder html(8192); // Reserve for large testing instructions page
        html.append("<!DOCTYPE html>\n");
        html.append("<html><head>\n");
        html.append("<title>HttpFileServer - Testing Guide</title>\n");
        html.append("<style>\n");
        html.append("body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, "
                    "sans-serif; margin: 0; padding: 20px; background: "
                    "linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: "
                    "#333; }\n");
        html.append(".container { max-width: 1200px; margin: 0 auto; "
                    "background: white; border-radius: 12px; box-shadow: 0 "
                    "10px 30px rgba(0,0,0,0.2); overflow: hidden; }\n");
        html.append(
            ".header { background: linear-gradient(135deg, #4facfe 0%, #00f2fe "
            "100%); color: white; padding: 30px; text-align: center; }\n");
        html.append(
            ".header h1 { margin: 0; font-size: 2.5em; font-weight: 300; }\n");
        html.append(".header p { margin: 10px 0 0 0; opacity: 0.9; font-size: "
                    "1.1em; }\n");
        html.append(".content { padding: 30px; }\n");
        html.append(
            ".section { margin: 30px 0; padding: 20px; border-left: 4px solid "
            "#4facfe; background: #f8f9fa; border-radius: 0 8px 8px 0; }\n");
        html.append(".section h2 { color: #2c3e50; margin-top: 0; display: "
                    "flex; align-items: center; gap: 10px; }\n");
        html.append(".section h3 { color: #34495e; margin-top: 20px; }\n");
        html.append(
            "code { background: #f1f2f6; padding: 2px 6px; border-radius: 3px; "
            "font-family: 'Courier New', monospace; color: #e74c3c; }\n");
        html.append(".url { background: #2c3e50; color: #ecf0f1; padding: 8px "
                    "12px; border-radius: 4px; font-family: 'Courier New', "
                    "monospace; display: inline-block; margin: 5px 0; }\n");
        html.append(".url a { color: #5dade2; font-weight: 500; }\n");
        html.append(".url a:hover { color: #85c1e9; }\n");
        html.append(".checklist { list-style: none; padding: 0; }\n");
        html.append(".checklist li { margin: 8px 0; padding: 8px 0; "
                    "border-bottom: 1px solid #ecf0f1; }\n");
        html.append(".checklist li:before { content: '✓ '; color: #27ae60; "
                    "font-weight: bold; }\n");
        html.append(
            ".warning { background: #fff3cd; border: 1px solid #ffeaa7; "
            "border-radius: 4px; padding: 12px; margin: 10px 0; }\n");
        html.append(
            ".success { background: #d4edda; border: 1px solid #c3e6cb; "
            "border-radius: 4px; padding: 12px; margin: 10px 0; }\n");
        html.append("a { color: #2980b9; text-decoration: none; }\n");
        html.append(
            "a:hover { text-decoration: underline; color: #3498db; }\n");
        html.append(".footer { background: #2c3e50; color: white; padding: "
                    "20px; text-align: center; }\n");
        html.append("</style></head><body>\n");

        html.append("<div class=\"container\">\n");
        html.append("<div class=\"header\">\n");
        html.append("<h1>🚀 HttpFileServer</h1>\n");
        html.append("<p>Complete Browser Testing Guide</p>\n");
        html.append("</div>\n");

        html.append("<div class=\"content\">\n");

        // Authentication Section
        html.append("<div class=\"section\">\n");
        html.append("<h2>🔐 Authentication</h2>\n");
        html.append("<p><strong>Server Credentials:</strong></p>\n");
        html.append("<div class=\"url\">Username: admin</div>\n");
        html.append("<div class=\"url\">Password: secret</div>\n");
        html.append("<p>Browser will show a login dialog when you access any "
                    "URL.</p>\n");
        html.append("<div class=\"success\">✓ All requests are logged to "
                    "<code>access.log</code></div>\n");
        html.append("</div>\n");

        // File Serving Section
        html.append("<div class=\"section\">\n");
        html.append("<h2>📄 File Serving</h2>\n");
        html.append("<h3>Test Files:</h3>\n");
        html.append("<div class=\"url\"><a "
                    "href=\"/index.html\">/index.html</a></div>\n");
        html.append(
            "<div class=\"url\"><a href=\"/style.css\">/style.css</a></div>\n");
        html.append("<div class=\"url\"><a href=\"/app.js\">/app.js</a> (view "
                    "source)</div>\n");
        html.append(
            "<div class=\"url\"><a href=\"/jstest.html\">/jstest.html</a> "
            "(runs test.js)</div>\n");
        html.append("<h3>Large File Download Test:</h3>\n");
        html.append("<div class=\"url\"><a "
                    "href=\"/large500MB.bin\">/large500MB.bin</a> (500 MB "
                    "binary file)</div>\n");
        html.append(
            "<p>Files are served with correct MIME types and UTF-8 encoding. "
            "Large file tests download speed and keep-alive behavior.</p>\n");
        html.append("</div>\n");

        // Directory Listing Section
        html.append("<div class=\"section\">\n");
        html.append("<h2>📁 Directory Listing</h2>\n");
        html.append("<h3>Subdirectory:</h3>\n");
        html.append(
            "<div class=\"url\"><a href=\"/files/\">/files/</a></div>\n");
        html.append("<p>Enhanced directory listings with file sizes, dates, "
                    "and emoji icons. ");
        html.append("Click on files to view them (browser will display or "
                    "download based on MIME type).</p>\n");
        html.append("</div>\n");

        // Access Control Section
        html.append("<div class=\"section\">\n");
        html.append("<h2>🚫 Access Control</h2>\n");
        html.append("<h3>Blocked Files (should return 403):</h3>\n");
        html.append("<div class=\"url\"><a "
                    "href=\"/config.conf\">/config.conf</a></div>\n");
        html.append("<div class=\"url\"><a "
                    "href=\"/server.log\">/server.log</a></div>\n");
        html.append(
            "<div class=\"url\"><a href=\"/temp.tmp\">/temp.tmp</a></div>\n");
        html.append("<div class=\"warning\">⚠️ These files are blocked by "
                    "access control rules</div>\n");
        html.append("</div>\n");

        // Performance Section
        html.append("<div class=\"section\">\n");
        html.append("<h2>⚡ Performance Features</h2>\n");
        html.append("<h3>1. File Cache:</h3>\n");
        html.append(
            "<ul><li>Enable with <code>config.enableCache = "
            "true</code></li><li>Caches file content in memory for faster "
            "serving</li><li>Auto-invalidates when file modification time "
            "changes</li><li>Skips cache for URLs with query strings "
            "(<code>?param=value</code>)</li><li>View cache stats: "
            "<code>server.getFileCache().size()</code> and "
            "<code>totalBytes()</code></li></ul>\n");
        html.append("<h3>2. ETag Support:</h3>\n");
        html.append("<ul><li>Load any file twice</li><li>Second request should "
                    "return <code>304 Not Modified</code></li><li>Check "
                    "Network tab in browser dev tools</li></ul>\n");
        html.append("<h3>3. Last-Modified Headers:</h3>\n");
        html.append(
            "<ul><li>Check Response Headers in dev tools</li><li>Should see "
            "<code>Last-Modified</code> field</li></ul>\n");
        html.append("</div>\n");

        // Error Handling Section
        html.append("<div class=\"section\">\n");
        html.append("<h2>❌ Error Handling</h2>\n");
        html.append("<h3>Test Error Pages:</h3>\n");
        html.append(
            "<div class=\"url\"><a "
            "href=\"/nonexistent.html\">/nonexistent.html</a> (404)</div>\n");
        html.append(
            "<div class=\"url\"><a href=\"/../etc/passwd\">/../etc/passwd</a> "
            "(404 in browser*, 403 via curl --path-as-is)</div>\n");
        html.append("<p style=\"font-size: 0.9em; color: #666;\">*Browsers "
                    "normalize URLs before sending, so the server receives "
                    "/etc/passwd (404). Use curl --path-as-is to test actual "
                    "path traversal protection (403).</p>\n");
        html.append(
            "<p>Custom styled error pages with helpful information.</p>\n");
        html.append("</div>\n");

        // Developer Tools Section
        html.append("<div class=\"section\">\n");
        html.append("<h2>🔍 Developer Tools Testing</h2>\n");
        html.append("<h3>Open Chrome DevTools (F12):</h3>\n");
        html.append(
            "<ul><li><strong>Network Tab:</strong> See all requests, status "
            "codes, headers</li><li><strong>Console Tab:</strong> Should see "
            "no errors for valid files</li><li><strong>Application "
            "Tab:</strong> Check browser caching</li></ul>\n");
        html.append("</div>\n");

        // Security Section
        html.append("<div class=\"section\">\n");
        html.append("<h2>🛡️ Security Features</h2>\n");
        html.append("<ul><li><strong>Path Traversal Protection:</strong> "
                    "Blocks <code>../</code> attacks</li><li><strong>Access "
                    "Control:</strong> Blocks sensitive file "
                    "types</li><li><strong>Authentication:</strong> Basic auth "
                    "required</li><li><strong>Security Headers:</strong> "
                    "X-Content-Type-Options, X-Frame-Options</li></ul>\n");
        html.append("</div>\n");

        // Testing Checklist
        html.append("<div class=\"section\">\n");
        html.append("<h2>🧪 Testing Checklist</h2>\n");
        html.append("<ul class=\"checklist\">\n");
        html.append("<li>Authentication prompt appears</li>\n");
        html.append("<li>Valid credentials grant access</li>\n");
        html.append("<li>Invalid credentials show 401 error</li>\n");
        html.append("<li>Directory listings show file metadata</li>\n");
        html.append("<li>Files download with correct MIME types</li>\n");
        html.append("<li>Sensitive files return 403 errors</li>\n");
        html.append("<li>Non-existent files return 404 errors</li>\n");
        html.append("<li>Path traversal attempts are blocked</li>\n");
        html.append("<li>ETag headers work (304 responses)</li>\n");
        html.append("<li>Access log records all requests</li>\n");
        html.append("<li>Custom headers appear in responses</li>\n");
        html.append("</ul>\n");
        html.append("</div>\n");

        html.append("</div>\n");
        html.append(
            CustomFileServerHtmlHelpers::generateDirectoryListingFooter());

        return html.toString();
    }

    /// Generate demo page that executes test.js
    std::string generateDemoPage() const {
        StringBuilder html(2048);
        html.append(CustomFileServerHtmlHelpers::generateDemoPageStart());
        html.append(".container { max-width: 800px; margin: 0 auto; "
                    "background: white; border-radius: 12px; box-shadow: 0 "
                    "10px 30px rgba(0,0,0,0.2); padding: 30px; }\n");
        html.append("h1 { color: #2c3e50; margin-top: 0; }\n");
        html.append(
            ".info { background: #e3f2fd; border-left: 4px solid #2196f3; "
            "padding: 15px; margin: 20px 0; border-radius: 4px; }\n");
        html.append(
            "#output { background: #f5f5f5; border: 1px solid #ddd; padding: "
            "15px; border-radius: 4px; min-height: 100px; font-family: "
            "'Courier New', monospace; white-space: pre-wrap; }\n");
        html.append("button { background: #4CAF50; color: white; border: none; "
                    "padding: 10px 20px; border-radius: 4px; cursor: pointer; "
                    "font-size: 16px; margin: 10px 5px 10px 0; }\n");
        html.append("button:hover { background: #45a049; }\n");
        html.append(".back-link { display: inline-block; margin-top: 20px; "
                    "color: #3498db; text-decoration: none; }\n");
        html.append(".back-link:hover { text-decoration: underline; }\n");
        html.append("</style>\n");
        html.append("<script src=\"/app.js\"></script>\n");
        html.append("</head><body>\n");
        html.append("<div class=\"container\">\n");
        html.append("<h1>🎯 JavaScript Execution Demo</h1>\n");
        html.append("<div class=\"info\">\n");
        html.append("<strong>ℹ️ Info:</strong> This page loads and executes "
                    "<code>/app.js</code> using a <code>&lt;script "
                    "src=\"/app.js\"&gt;&lt;/script&gt;</code> tag.\n");
        html.append("</div>\n");
        html.append("<h2>Output:</h2>\n");
        html.append(
            "<div id=\"output\">Waiting for JavaScript to execute...</div>\n");
        html.append(
            "<button onclick=\"testFunction()\">Run Test Function</button>\n");
        html.append(
            "<button onclick=\"location.reload()\">Reload Page</button>\n");
        html.append("<p><a href=\"/\" class=\"back-link\">← Back to Testing "
                    "Guide</a></p>\n");
        html.append("</div>\n");
        html.append("<script>\n");
        html.append("// Display that the script loaded\n");
        html.append("document.getElementById('output').textContent = 'test.js "
                    "loaded successfully!\\n\\nClick \"Run Test Function\" to "
                    "execute the test function from test.js';\n");
        html.append("</script>\n");
        html.append("</body></html>\n");

        return html.toString();
    }

    /// Generate 500MB test file on-the-fly
    void generateLargeFile(HttpClientState& state, const HttpRequest& request) {
        const size_t fileSize = 500 * 1024 * 1024; // 500 MB

        StringBuilder response(256);
        response.append("HTTP/1.1 200 OK\r\n");
        response.append("Content-Type: application/octet-stream\r\n");
        response.append(
            "Content-Disposition: attachment; filename=\"large500MB.bin\"\r\n");
        response.appendFormat("Content-Length: %zu\r\n", fileSize);

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
            state.responseBuf = response.toString();
            state.responseView = state.responseBuf;
            return;
        }

        // For GET requests, generate the file content
        std::string headers = response.toString();
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
        StringBuilder html(4096); // Reserve for enhanced directory listing HTML
        html.append("<!DOCTYPE html>\n");
        html.append("<html><head>\n");
        html.append("<title>Directory: ");
        html.append(dirPath);
        html.append("</title>\n");
        html.append("<style>\n");
        html.append("body { font-family: Arial, sans-serif; margin: 20px; }\n");
        html.append("table { border-collapse: collapse; width: 100%; }\n");
        html.append("th, td { padding: 8px; text-align: left; border-bottom: "
                    "1px solid #ddd; }\n");
        html.append("th { background-color: #f2f2f2; }\n");
        html.append("a { text-decoration: none; color: #3498db; }\n");
        html.append("a:hover { text-decoration: underline; }\n");
        html.append("</style>\n");
        html.append("</head><body>\n");
        html.append("<h1>Directory listing: ");
        html.append(dirPath);
        html.append("</h1>\n");
        html.append("<table>\n");
        html.append("<tr><th>Name</th><th>Type</th><th>Size</th><th>Modified</"
                    "th></tr>\n");

        // Add parent directory link
        if (dirPath != getConfig().documentRoot) {
            html.append("<tr><td><a "
                        "href=\"../\">../</a></td><td>Directory</td><td>-</"
                        "td><td>-</td></tr>\n");
            html.append(
                "<tr><td><a href=\"../\">📁 "
                "../</a></td><td>Directory</td><td>-</td><td>-</td></tr>\n");
        }

        std::vector<std::pair<std::string, bool>> entries; // name, isDirectory

        std::vector<PathHelper::DirEntry> dirEntries
            = PathHelper::listDirectory(dirPath);
        for (const auto& entry : dirEntries) {
            const std::string& name = entry.name;
            if (name.empty() || name[0] == '.') continue; // Skip hidden files

            bool isDir = entry.isDirectory;
            entries.emplace_back(name, isDir);
        }

        // Sort entries: directories first, then files, both alphabetically
        std::sort(
            entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                if (a.second != b.second)
                    return a.second > b.second; // directories first
                return a.first < b.first; // alphabetical
            });

        for (const auto& [name, isDir] : entries) {
            std::string type = isDir ? "Directory" : getFileExtension(name);
            std::string size = "-";
            std::string modified = "-";

            if (!isDir) {
                std::string fullPath = PathHelper::joinPath(dirPath, name);
                PathHelper::FileInfo fileInfo
                    = PathHelper::getFileInfo(fullPath);
                if (fileInfo.exists) {
                    size_t fileSize = fileInfo.size;
                    if (fileSize < 1024) {
                        size = std::to_string(fileSize) + " B";
                    } else if (fileSize < 1024 * 1024) {
                        size = std::to_string(fileSize / 1024) + " KB";
                    } else {
                        size = std::to_string(fileSize / (1024 * 1024)) + " MB";
                    }

                    std::time_t cftime = fileInfo.lastModified;

                    char buffer[32];
#ifdef _WIN32
                    struct tm timeinfo = {};
                    localtime_s(&timeinfo, &cftime);
                    strftime(
                        buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &timeinfo);
#else
                    struct tm* timeinfo = localtime(&cftime);
                    strftime(
                        buffer, sizeof(buffer), "%Y-%m-%d %H:%M", timeinfo);
#endif
                    modified = std::string(buffer);
                }
            }

            html.append("<tr><td><a href=\"");
            html.append(name);
            if (isDir) {
                html.append("/");
            }
            html.append("\">");
            html.append(isDir ? "📁" : "📄");
            html.append(" ");
            html.append(name);
            if (isDir) {
                html.append("/");
            }
            html.append("</a></td>");
            html.append("<td>");
            html.append(type);
            html.append("</td>");
            html.append("<td>");
            html.append(size);
            html.append("</td>");
            html.append("<td>");
            html.append(modified);
            html.append("</td></tr>\n");
        }

        html.append("</table>\n");
        html.append("<hr><p><small>Custom File Server | ");
        html.append(getCurrentTime());
        html.append("</small></p>\n");
        html.append("</body></html>");
        return html.toString();
    }

    private:
    File logFile_;

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
            static_cast<int>(timeinfo.tm_mday),
            static_cast<int>(timeinfo.tm_hour),
            static_cast<int>(timeinfo.tm_min),
            static_cast<int>(timeinfo.tm_sec), request.method.c_str(),
            request.path.c_str());
#else
        struct tm* timeinfo = localtime(&time_t);
        logFile_.printf(
            "%04d-%02d-%02d %02d:%02d:%02d %s %s from client\n", //-V111 //-V111
            static_cast<int>(timeinfo->tm_year + 1900),
            static_cast<int>(timeinfo->tm_mon + 1),
            static_cast<int>(timeinfo->tm_mday),
            static_cast<int>(timeinfo->tm_hour),
            static_cast<int>(timeinfo->tm_min),
            static_cast<int>(timeinfo->tm_sec), request.method.c_str(),
            request.path.c_str());
#endif
        logFile_.flush();
    }

    bool isAuthenticated(const HttpRequest& request) const {
        // Simple basic authentication (username: admin, password: secret)
        auto authIt = request.headers.find("authorization");
        if (authIt == request.headers.end()) {
            return false;
        }

        const std::string& auth = authIt->second;
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

        StringBuilder response(
            256 + htmlBody.size()); // Reserve for headers + body
        response.append("HTTP/1.1 401 Unauthorized\r\n");
        response.append("Content-Type: text/html\r\nContent-Length: ");
        response.appendFormat("%zu", htmlBody.size());
        response.append(
            "\r\nWWW-Authenticate: Basic realm=\"Secure Area\"\r\n\r\n");
        response.append(htmlBody);

        state.responseBuf = response.toString();
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
};
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

    server.run();

    printf("%s", ServerStrings::SERVER_STOPPED);
    printf("%s", ServerStrings::LOG_SAVED);
    printf("%s", ServerStrings::THANK_YOU);

    return 0;
}
