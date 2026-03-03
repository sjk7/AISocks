// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include "HttpPollServer.h"
#include "HttpRequest.h"
#include "FileIO.h"
#include "UrlCodec.h"
#include "PathHelper.h"
#include "FileCache.h"
#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <iomanip>

#ifdef _WIN32
    #include <direct.h>
#else
    #include <unistd.h>
#endif

namespace aiSocks {

/// HTTP file server that serves files from the current directory.
/// This is a base class with virtual customization points for:
/// - File path resolution
/// - MIME type detection
/// - Error handling
/// - Access control
/// - Custom headers
class HttpFileServer : public HttpPollServer {
public:
    /// Configuration for the file server
    struct Config {
        std::string documentRoot;                    // Root directory for serving files
        std::string indexFile;                       // Default file for directory requests
        bool enableDirectoryListing = false;         // Show directory contents when no index file
        bool enableETag = false;                     // Generate ETag headers for files
        bool enableLastModified = false;             // Generate Last-Modified headers
        size_t maxFileSize = 0;                      // Max file size to serve (100MB default)
        bool enableCache = false;                    // Enable file content caching
        bool enableSecurityHeaders = false;          // Add security headers (X-Content-Type-Options, etc.)
        bool hideServerVersion = false;              // Hide server version in error pages
        std::map<std::string, std::string> customHeaders; // Additional headers to add
    };

    explicit HttpFileServer(const ServerBind& bind, const Config& config)
        : HttpPollServer(bind) {
        // Set default values
        config_.documentRoot = config.documentRoot.empty() ? "." : config.documentRoot;
        config_.indexFile = config.indexFile.empty() ? "index.html" : config.indexFile;
        config_.enableDirectoryListing = config.enableDirectoryListing;
        config_.enableETag = config.enableETag ? config.enableETag : true;
        config_.enableLastModified = config.enableLastModified ? config.enableLastModified : true;
        config_.maxFileSize = config.maxFileSize ? config.maxFileSize : 100 * 1024 * 1024;
        config_.enableCache = config.enableCache; // Default false
        config_.enableSecurityHeaders = config.enableSecurityHeaders ? config.enableSecurityHeaders : true;
        config_.hideServerVersion = config.hideServerVersion ? config.hideServerVersion : true;
        config_.customHeaders = config.customHeaders;
        
        // Normalize document root path
        if (!config_.documentRoot.empty() && config_.documentRoot.back() != '/') {
            config_.documentRoot += '/';
        }
    }

protected:
    /// Main request handler - routes to appropriate handlers
    void buildResponse(HttpClientState& state) override {
        // Parse the HTTP request
        auto request = HttpRequest::parse(state.request);
        
        if (!request.valid) {
            sendError(state, 400, "Bad Request", "Invalid HTTP request");
            return;
        }

        // Only handle GET and HEAD requests
        if (request.method != "GET" && request.method != "HEAD") {
            sendError(state, 405, "Method Not Allowed", "Only GET and HEAD methods are supported");
            return;
        }

        // Resolve the file path (includes URL decoding)
        std::string filePath = resolveFilePath(request.path);
        
        // DEBUG: Print EVERY request
        printf("\n[DEBUG] ===== REQUEST RECEIVED =====\n");
        printf("  Method: %s\n", request.method.c_str());
        printf("  Request path: %s\n", request.path.c_str());
        printf("  Resolved file path: %s\n", filePath.c_str());
        printf("  Document root: %s\n", config_.documentRoot.c_str());
        printf("  Contains '..'? %s\n", request.path.find("..") != std::string::npos ? "YES" : "NO");
        fflush(stdout);
        
        // ═══════════════════════════════════════════════════════════════════════
        // CRITICAL SECURITY CHECK: PATH TRAVERSAL PREVENTION VIA CANONICALIZATION
        // ═══════════════════════════════════════════════════════════════════════
        //
        // WHAT WE'RE PROTECTING AGAINST:
        // Path traversal attacks attempt to access files outside the document root
        // by using ".." (parent directory) components in the URL path.
        //
        // ATTACK EXAMPLES:
        //   GET /../../../etc/passwd
        //   GET /../../../Windows/System32/config/SAM
        //   GET /images/../../config/database.yml
        //
        // HOW THE ATTACK WORKS:
        // 1. User requests: GET /../../../etc/passwd
        // 2. resolveFilePath() combines: documentRoot + requestPath
        //    Result: "www/../../../etc/passwd"
        // 3. Without canonicalization, simple string check would see "www/" prefix
        //    and incorrectly allow access!
        //
        // HOW CANONICALIZATION PREVENTS THIS:
        // 1. PathHelper::isPathWithin() canonicalizes both paths:
        //    - "www/../../../etc/passwd" → "/etc/passwd" (resolves all ..)
        //    - "www/" → "/home/user/project/www/" (absolute path)
        // 2. Checks if "/etc/passwd" starts with "/home/user/project/www/"
        // 3. NO → Returns false → We send 403 Forbidden
        //
        // LEGITIMATE FILE ACCESS:
        // 1. User requests: GET /images/logo.png
        // 2. resolveFilePath(): "www/images/logo.png"
        // 3. Canonicalization:
        //    - "www/images/logo.png" → "/home/user/project/www/images/logo.png"
        //    - "www/" → "/home/user/project/www/"
        // 4. Check passes → Continue to file existence check
        //
        // NONEXISTENT FILES (404 vs 403):
        // 1. User requests: GET /missing.html
        // 2. resolveFilePath(): "www/missing.html" (doesn't exist)
        // 3. On Unix, realpath() fails for nonexistent files
        // 4. isPathWithin() uses normalizePathManual() fallback
        // 5. Manual normalization still resolves ".." correctly
        // 6. Security check passes → File doesn't exist → Return 404 (not 403)
        //
        // WHY THIS MATTERS:
        // - 403 Forbidden: "You're not allowed to access this" (security block)
        // - 404 Not Found: "This file doesn't exist" (normal behavior)
        // Returning 403 for nonexistent files would leak information about
        // the file system structure to attackers.
        //
        if (!PathHelper::isPathWithin(filePath, config_.documentRoot)) {
            printf("[DEBUG] Path is NOT within document root - returning 403 Forbidden\n");
            fflush(stdout);
            sendError(state, 403, "Forbidden", "Access denied");
            return;
        }
        
        if (request.path.find("..") != std::string::npos) {
            printf("[DEBUG] Path traversal check passed - continuing to file existence check\n");
            fflush(stdout);
        }
        
        // Check if path exists and get file info
        FileInfo fileInfo = getFileInfo(filePath);
        
        if (!fileInfo.exists) {
            sendError(state, 404, "Not Found", "File not found");
            return;
        }

        if (fileInfo.isDirectory) {
            handleDirectoryRequest(state, filePath, request);
        } else {
            handleFileRequest(state, filePath, fileInfo, request);
        }
    }

    /// Virtual customization point: resolve file path from request target
    virtual std::string resolveFilePath(const std::string& target) const {
        // Remove query string and fragment
        std::string path = target;
        size_t queryPos = path.find('?');
        if (queryPos != std::string::npos) {
            path = path.substr(0, queryPos);
        }
        size_t fragmentPos = path.find('#');
        if (fragmentPos != std::string::npos) {
            path = path.substr(0, fragmentPos);
        }

        // Decode URL encoding (use path-specific decoding, not form encoding)
        path = urlDecodePath(path);

        // Handle root path
        if (path == "/") {
            path = "/" + config_.indexFile;
        }

        // Construct full file path
        return config_.documentRoot + path.substr(1); // Remove leading '/'
    }

    /// File information structure
    struct FileInfo {
        bool exists = false;
        bool isDirectory = false;
        size_t size = 0;
        time_t lastModified = 0;
        std::string etag;
    };

    /// Virtual customization point: validate file size
    /// Returns true if the file size is acceptable, false otherwise
    virtual bool isFileSizeAcceptable(const std::string& /*filePath*/, size_t fileSize) const {
        return fileSize <= config_.maxFileSize;
    }

    /// Virtual customization point: get file information
    virtual FileInfo getFileInfo(const std::string& filePath) const {
        FileInfo info;
        
        PathHelper::FileInfo pathInfo = PathHelper::getFileInfo(filePath);
        
        if (!pathInfo.exists) {
            return info; // exists = false
        }

        info.exists = true;
        info.isDirectory = pathInfo.isDirectory;
        
        if (!info.isDirectory) {
            info.size = pathInfo.size;
            info.lastModified = pathInfo.lastModified;
            
            // Generate ETag based on file size and modification time
            if (config_.enableETag) {
                StringBuilder etag(64); // Reserve for ETag format
                etag.appendFormat("\"%zu-%ld\"", 
                    info.size,
                    static_cast<long>(info.lastModified));
                info.etag = etag.toString();
            }
        }
        
        return info;
    }

    /// Virtual customization point: get MIME type for file
    virtual std::string getMimeType(const std::string& filePath) const {
        // Extract extension from file path to determine MIME type
        std::string ext = getFileExtension(filePath);
        
        // Common MIME types
        static const std::map<std::string, std::string> mimeTypes = {
            {".html", "text/html"},
            {".htm", "text/html"},
            {".css", "text/css"},
            {".js", "application/javascript"},
            {".json", "application/json"},
            {".xml", "application/xml"},
            {".txt", "text/plain"},
            {".md", "text/markdown"},
            {".png", "image/png"},
            {".jpg", "image/jpeg"},
            {".jpeg", "image/jpeg"},
            {".gif", "image/gif"},
            {".svg", "image/svg+xml"},
            {".ico", "image/x-icon"},
            {".pdf", "application/pdf"},
            {".zip", "application/zip"},
            {".gz", "application/gzip"},
            {".mp3", "audio/mpeg"},
            {".mp4", "video/mp4"},
            {".webm", "video/webm"},
            {".woff", "font/woff"},
            {".woff2", "font/woff2"},
            {".ttf", "font/ttf"},
            {".eot", "application/vnd.ms-fontobject"}
        };
        
        auto it = mimeTypes.find(ext);
        if (it != mimeTypes.end()) {
            return it->second;
        }
        
        return "application/octet-stream"; // Default binary type
    }

    /// Virtual customization point: check if file access is allowed
    virtual bool isAccessAllowed(const std::string& filePath, const FileInfo& fileInfo) const {
        // Basic checks using file info
        if (!fileInfo.exists) return false;
        // Note: File size is already checked via TOCTOU-safe descriptor info before this call
        
        // Block access to hidden files (dotfiles) - prevents access to .htpasswd, .git, etc.
        size_t lastSlash = filePath.find_last_of("/\\");
        std::string filename = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;
        if (!filename.empty() && filename[0] == '.') {
            return false;
        }
        
        // Additional checks can be added by derived classes
        return true;
    }

    /// Virtual customization point: handle directory requests
    virtual void handleDirectoryRequest(HttpClientState& state, const std::string& dirPath, const HttpRequest& request) {
        // Try to serve index file
        std::string indexPath = dirPath + "/" + config_.indexFile;
        FileInfo indexInfo = getFileInfo(indexPath);
        
        if (indexInfo.exists && !indexInfo.isDirectory) {
            handleFileRequest(state, indexPath, indexInfo, request);
            return;
        }
        
        // If no index file and directory listing is enabled
        if (config_.enableDirectoryListing) {
            sendDirectoryListing(state, dirPath);
        } else {
            sendError(state, 403, "Forbidden", "Directory listing not allowed");
        }
    }

    /// Virtual customization point: handle file requests
    virtual void handleFileRequest(HttpClientState& state, const std::string& filePath, const FileInfo& fileInfo, const HttpRequest& request) {
        // Check if we should use cache (no query string in request)
        bool useCache = config_.enableCache && request.path.find('?') == std::string::npos;
        
        // Try cache first if enabled
        if (useCache) {
            const FileCache::CachedFile* cached = fileCache_.get(filePath, fileInfo.lastModified);
            if (cached) {
                // Cache hit! Use cached content
                sendCachedFile(state, filePath, *cached, fileInfo, request);
                return;
            }
        }
        
        // TOCTOU-safe file handling: open file once, lock it, check descriptor, read from same descriptor
        File file(filePath.c_str(), "rb");
        if (!file.isOpen()) {
            sendError(state, 500, "Internal Server Error", "Failed to open file");
            return;
        }
        
        // Get file info from the locked descriptor (TOCTOU-safe)
        File::FileInfo fdInfo = file.getInfoFromDescriptor();
        if (!fdInfo.valid) {
            sendError(state, 500, "Internal Server Error", "Failed to get file information");
            return;
        }
        
        // Security checks on the locked file descriptor
        if (fdInfo.isSymlink) {
            sendError(state, 403, "Forbidden", "Symlinks are not allowed");
            return;
        }
        
        if (!fdInfo.isRegular) {
            sendError(state, 403, "Forbidden", "Not a regular file");
            return;
        }
        
        if (!isFileSizeAcceptable(filePath, fdInfo.size)) {
            sendError(state, 403, "Forbidden", "File too large");
            return;
        }
        
        if (!isAccessAllowed(filePath, fileInfo)) {
            sendError(state, 403, "Forbidden", "Access to this file is not allowed");
            return;
        }

        // Check conditional headers using original fileInfo (from path-based check)
        if (config_.enableETag && !fileInfo.etag.empty()) {
            auto ifNoneMatch = request.headers.find("if-none-match");
            if (ifNoneMatch != request.headers.end() && ifNoneMatch->second == fileInfo.etag) {
                sendNotModified(state, fileInfo);
                return;
            }
        }

        if (config_.enableLastModified) {
            auto ifModifiedSince = request.headers.find("if-modified-since");
            if (ifModifiedSince != request.headers.end()) {
                StringBuilder lastModified(64); // Reserve for HTTP date format
                lastModified.append(formatHttpDate(fileInfo.lastModified));
                if (ifModifiedSince->second == lastModified.toString()) {
                    sendNotModified(state, fileInfo);
                    return;
                }
            }
        }

        // Read from the locked file descriptor (TOCTOU-safe)
        std::vector<char> fileContent = file.readAll();
        if (fileContent.empty() && fdInfo.size > 0) {
            sendError(state, 500, "Internal Server Error", "Failed to read file");
            return;
        }
        
        // Update cache if enabled
        if (useCache) {
            fileCache_.put(filePath, fileContent, fileInfo.lastModified);
        }

        // Build response
        StringBuilder response(512 + fileContent.size()); // Reserve for headers + content
        response.append("HTTP/1.1 200 OK\r\n");
        response.append("Content-Type: ");
        response.append(getMimeType(filePath));
        // Add charset for text content to ensure proper UTF-8 display
        if (getMimeType(filePath).find("text/") == 0 || getMimeType(filePath) == "application/javascript") {
            response.append("; charset=utf-8");
        }
        response.append("\r\nContent-Length: ");
        response.appendFormat("%zu", fileContent.size());
        response.append("\r\n");
        
        if (config_.enableLastModified) {
            response.append("Last-Modified: ");
            response.append(formatHttpDate(fileInfo.lastModified));
            response.append("\r\n");
        }
        
        if (config_.enableETag && !fileInfo.etag.empty()) {
            response.append("ETag: ");
            response.append(fileInfo.etag);
            response.append("\r\n");
        }
        
        // Add security headers
        addSecurityHeaders(response);
        
        // Add custom headers
        for (const auto& [name, value] : config_.customHeaders) {
            response.append(name);
            response.append(": ");
            response.append(value);
            response.append("\r\n");
        }
        
        response.append("\r\n");
        
        state.responseBuf = response.toString() + std::string(fileContent.begin(), fileContent.end());
        state.responseView = state.responseBuf;
    }

    /// Virtual customization point: send error response
    virtual void sendError(HttpClientState& state, int code, const std::string& status, const std::string& message) {
        std::string htmlBody = generateErrorHtml(code, status, message);
        
        StringBuilder response(256 + htmlBody.size()); // Reserve for headers + body
        response.append("HTTP/1.1 ");
        response.appendFormat("%d", code);
        response.append(" ");
        response.append(status);
        response.append("\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ");
        response.appendFormat("%zu", htmlBody.size());
        response.append("\r\n");
        
        // Add security headers
        addSecurityHeaders(response);
        
        // Add custom headers
        for (const auto& [name, value] : config_.customHeaders) {
            response.append(name);
            response.append(": ");
            response.append(value);
            response.append("\r\n");
        }
        
        response.append("\r\n");
        response.append(htmlBody);
        
        state.responseBuf = response.toString();
        state.responseView = state.responseBuf;
    }

    /// Virtual customization point: send 304 Not Modified response
    virtual void sendNotModified(HttpClientState& state, const FileInfo& fileInfo) {
        StringBuilder response(256); // Reserve for 304 response headers
        response.append("HTTP/1.1 304 Not Modified\r\n");
        
        if (config_.enableLastModified) {
            response.append("Last-Modified: ");
            response.append(formatHttpDate(fileInfo.lastModified));
            response.append("\r\n");
        }
        
        if (config_.enableETag && !fileInfo.etag.empty()) {
            response.append("ETag: ");
            response.append(fileInfo.etag);
            response.append("\r\n");
        }
        
        // Add security headers
        addSecurityHeaders(response);
        
        // Add custom headers
        for (const auto& [name, value] : config_.customHeaders) {
            response.append(name);
            response.append(": ");
            response.append(value);
            response.append("\r\n");
        }
        
        response.append("\r\n");
        
        state.responseBuf = response.toString();
        state.responseView = state.responseBuf;
    }
    
    /// Send cached file content
    void sendCachedFile(HttpClientState& state, const std::string& filePath, 
                        const FileCache::CachedFile& cached, const FileInfo& fileInfo,
                        const HttpRequest& request) {
        (void)request; // May be used for conditional headers in future
        
        // Build response from cached content
        StringBuilder response(512 + cached.size); // Reserve for headers + cached content
        response.append("HTTP/1.1 200 OK\r\n");
        response.append("Content-Type: ");
        response.append(getMimeType(filePath));
        if (getMimeType(filePath).find("text/") == 0 || getMimeType(filePath) == "application/javascript") {
            response.append("; charset=utf-8");
        }
        response.append("\r\nContent-Length: ");
        response.appendFormat("%zu", cached.size);
        response.append("\r\n");
        
        if (config_.enableLastModified) {
            response.append("Last-Modified: ");
            response.append(formatHttpDate(fileInfo.lastModified));
            response.append("\r\n");
        }
        
        if (config_.enableETag && !fileInfo.etag.empty()) {
            response.append("ETag: ");
            response.append(fileInfo.etag);
            response.append("\r\n");
        }
        
        // Add security headers
        addSecurityHeaders(response);
        
        // Add custom headers
        for (const auto& [name, value] : config_.customHeaders) {
            response.append(name);
            response.append(": ");
            response.append(value);
            response.append("\r\n");
        }
        
        response.append("\r\n");
        
        state.responseBuf = response.toString() + std::string(cached.content.begin(), cached.content.end());
        state.responseView = state.responseBuf;
    }

    /// Virtual customization point: send directory listing
    virtual void sendDirectoryListing(HttpClientState& state, const std::string& dirPath) {
        std::string htmlBody = generateDirectoryListing(dirPath);
        
        StringBuilder response(256 + htmlBody.size()); // Reserve for headers + body
        response.append("HTTP/1.1 200 OK\r\n");
        response.append("Content-Type: text/html; charset=utf-8\r\nContent-Length: ");
        response.appendFormat("%zu", htmlBody.size());
        response.append("\r\n");
        
        // Add security headers
        addSecurityHeaders(response);
        
        // Add custom headers
        for (const auto& [name, value] : config_.customHeaders) {
            response.append(name);
            response.append(": ");
            response.append(value);
            response.append("\r\n");
        }
        
        response.append("\r\n");
        response.append(htmlBody);
        
        state.responseBuf = response.toString();
        state.responseView = state.responseBuf;
    }

    /// Virtual customization point: generate error HTML
    virtual std::string generateErrorHtml(int code, const std::string& status, const std::string& message) const {
        StringBuilder html(512); // Reserve for error page HTML
        html.append("<!DOCTYPE html>\n");
        html.append("<html><head><title>");
        html.appendFormat("%d", code);
        html.append(" ");
        html.append(htmlEscape(status));
        html.append("</title></head>\n");
        html.append("<body><h1>");
        html.appendFormat("%d", code);
        html.append(" ");
        html.append(htmlEscape(status));
        html.append("</h1>\n");
        html.append("<p>");
        html.append(htmlEscape(message));
        html.append("</p>\n");
        if (!config_.hideServerVersion) {
            html.append("<hr><address>aiSocks HttpFileServer</address>\n");
        }
        html.append("</body></html>");
        return html.toString();
    }

    /// Virtual customization point: generate directory listing HTML
    virtual std::string generateDirectoryListing(const std::string& dirPath) const {
        StringBuilder html(2048); // Reserve for directory listing HTML
        html.append("<!DOCTYPE html>\n");
        html.append("<html><head><title>Directory listing</title></head>\n");
        html.append("<body><h1>Directory listing</h1>\n");
        html.append("<ul>\n");
        
        std::vector<PathHelper::DirEntry> entries = PathHelper::listDirectory(dirPath);
        if (entries.empty()) {
            html.append("<li>Error reading directory</li>\n");
        } else {
            for (const auto& entry : entries) {
                const std::string& name = entry.name;
                if (name.empty() || name[0] == '.') continue; // Skip hidden files
                
                bool isDir = entry.isDirectory;
                
                html.append("<li><a href=\"");
                html.append(urlEncode(name));
                if (isDir) {
                    html.append("/");
                }
                html.append("\">");
                html.append(htmlEscape(name));
                if (isDir) {
                    html.append("/");
                }
                html.append("</a></li>\n");
            }
        }
        
        html.append("</ul>\n");
        if (!config_.hideServerVersion) {
            html.append("<hr><address>aiSocks HttpFileServer</address>\n");
        }
        html.append("</body></html>");
        return html.toString();
    }

protected:
    const Config& getConfig() const { return config_; }
    FileCache& getFileCache() { return fileCache_; }
    
private:
    Config config_;
    FileCache fileCache_;
    
    /// Add security headers to response
    void addSecurityHeaders(StringBuilder& response) const {
        if (!config_.enableSecurityHeaders) return;
        
        response.append("X-Content-Type-Options: nosniff\r\n");
        response.append("X-Frame-Options: DENY\r\n");
        response.append("Content-Security-Policy: default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self'\r\n");
        response.append("Referrer-Policy: no-referrer\r\n");
    }
    
    /// HTML escape a string to prevent XSS
    static std::string htmlEscape(const std::string& str) {
        std::string escaped;
        escaped.reserve(str.size());
        for (char c : str) {
            switch (c) {
                case '&':  escaped.append("&amp;"); break;
                case '<':  escaped.append("&lt;"); break;
                case '>':  escaped.append("&gt;"); break;
                case '"':  escaped.append("&quot;"); break;
                case '\'': escaped.append("&#39;"); break;
                default:   escaped.push_back(c); break;
            }
        }
        return escaped;
    }
    
    /// URL decode for paths (does NOT treat + as space, unlike form encoding)
    static std::string urlDecodePath(const std::string& src) {
        static const auto fromHex = []() noexcept {
            std::array<uint8_t, 256> t{};
            t.fill(0xFF);
            for (int i = 0; i < 10; ++i)
                t[static_cast<unsigned>('0' + i)] = static_cast<uint8_t>(i);
            for (int i = 0; i < 6; ++i) {
                t[static_cast<unsigned>('A' + i)] = static_cast<uint8_t>(10 + i);
                t[static_cast<unsigned>('a' + i)] = static_cast<uint8_t>(10 + i);
            }
            return t;
        }();

        std::string out;
        out.reserve(src.size());
        for (size_t i = 0, n = src.size(); i < n; ++i) {
            const unsigned char c = static_cast<unsigned char>(src[i]);
            if (c == '%' && i + 2 < n) {
                const uint8_t hi = fromHex[static_cast<unsigned char>(src[i + 1])];
                const uint8_t lo = fromHex[static_cast<unsigned char>(src[i + 2])];
                if (hi != 0xFF && lo != 0xFF) {
                    out += static_cast<char>((hi << 4) | lo);
                    i += 2;
                    continue;
                }
            }
            // Do NOT treat + as space in paths (only in query strings)
            out += static_cast<char>(c);
        }
        return out;
    }

    std::string getFileExtension(const std::string& filePath) const {
        size_t dotPos = filePath.find_last_of('.');
        if (dotPos != std::string::npos && dotPos < filePath.length() - 1) {
            return filePath.substr(dotPos);
        }
        return "";
    }

    std::string formatHttpDate(time_t fileTime) const {
        // Use strftime directly for proper HTTP date format
        char buffer[32];
#ifdef _WIN32
        struct tm timeinfo = {};
        gmtime_s(&timeinfo, &fileTime);
        strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &timeinfo);
#else
        struct tm* timeinfo = gmtime(&fileTime);
        strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", timeinfo);
#endif
        
        return std::string(buffer);
    }
};

} // namespace aiSocks
