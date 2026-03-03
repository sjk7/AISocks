// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include "HttpPollServer.h"
#include "HttpRequest.h"
#include "FileIO.h"
#include "UrlCodec.h"
#include "PathHelper.h"
#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <iomanip>

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
        std::string documentRoot;           // Root directory for serving files
        std::string indexFile;              // Default file for directory requests
        bool enableDirectoryListing;        // Show directory contents when no index file
        bool enableETag;                    // Generate ETag headers for files
        bool enableLastModified;            // Generate Last-Modified headers
        size_t maxFileSize;                 // Max file size to serve (100MB)
        std::map<std::string, std::string> customHeaders; // Additional headers to add
    };

    explicit HttpFileServer(const ServerBind& bind, const Config& config = {})
        : HttpPollServer(bind) {
        // Set default values
        config_.documentRoot = config.documentRoot.empty() ? "." : config.documentRoot;
        config_.indexFile = config.indexFile.empty() ? "index.html" : config.indexFile;
        config_.enableDirectoryListing = config.enableDirectoryListing;
        config_.enableETag = config.enableETag ? config.enableETag : true;
        config_.enableLastModified = config.enableLastModified ? config.enableLastModified : true;
        config_.maxFileSize = config.maxFileSize ? config.maxFileSize : 100 * 1024 * 1024;
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
        
        // Security check: prevent path traversal (must be AFTER URL decoding)
        if (filePath.find("..") != std::string::npos) {
            sendError(state, 400, "Bad Request", "Path traversal not allowed");
            return;
        }
        
        // Additional security: prevent Windows backslash traversal
        if (filePath.find("\\") != std::string::npos) {
            sendError(state, 400, "Bad Request", "Backslash paths not allowed");
            return;
        }
        
        // Additional security: prevent absolute paths
        if (!filePath.empty() && (filePath[0] == '/' || filePath[0] == '\\')) {
            sendError(state, 400, "Bad Request", "Absolute paths not allowed");
            return;
        }
        
        // Additional security: ensure path starts with document root
        if (filePath.find(config_.documentRoot) != 0) {
            sendError(state, 400, "Bad Request", "Access denied");
            return;
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

        // Decode URL encoding (basic implementation)
        path = urlDecode(path);

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
                StringBuilder etag;
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
        (void)filePath; // Suppress unused parameter warning - available for derived classes
        if (!fileInfo.exists) return false;
        if (fileInfo.size > config_.maxFileSize) return false;
        
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
        
        if (fdInfo.size > config_.maxFileSize) {
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
                StringBuilder lastModified;
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

        // Build response
        StringBuilder response;
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
        
        StringBuilder response;
        response.append("HTTP/1.1 ");
        response.appendFormat("%d", code);
        response.append(" ");
        response.append(status);
        response.append("\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ");
        response.appendFormat("%zu", htmlBody.size());
        response.append("\r\n");
        
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
        StringBuilder response;
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

    /// Virtual customization point: send directory listing
    virtual void sendDirectoryListing(HttpClientState& state, const std::string& dirPath) {
        std::string htmlBody = generateDirectoryListing(dirPath);
        
        StringBuilder response;
        response.append("HTTP/1.1 200 OK\r\n");
        response.append("Content-Type: text/html; charset=utf-8\r\nContent-Length: ");
        response.appendFormat("%zu", htmlBody.size());
        response.append("\r\n");
        
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
        StringBuilder html;
        html.append("<!DOCTYPE html>\n");
        html.append("<html><head><title>");
        html.appendFormat("%d", code);
        html.append(" ");
        html.append(status);
        html.append("</title></head>\n");
        html.append("<body><h1>");
        html.appendFormat("%d", code);
        html.append(" ");
        html.append(status);
        html.append("</h1>\n");
        html.append("<p>");
        html.append(message);
        html.append("</p>\n");
        html.append("<hr><address>aiSocks HttpFileServer</address>\n");
        html.append("</body></html>");
        return html.toString();
    }

    /// Virtual customization point: generate directory listing HTML
    virtual std::string generateDirectoryListing(const std::string& dirPath) const {
        StringBuilder html;
        html.append("<!DOCTYPE html>\n");
        html.append("<html><head><title>Directory listing for ");
        html.append(dirPath);
        html.append("</title></head>\n");
        html.append("<body><h1>Directory listing for ");
        html.append(dirPath);
        html.append("</h1>\n");
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
                html.append(name);
                if (isDir) {
                    html.append("/");
                }
                html.append("\">");
                html.append(name);
                if (isDir) {
                    html.append("/");
                }
                html.append("</a></li>\n");
            }
        }
        
        html.append("</ul>\n");
        html.append("<hr><address>aiSocks HttpFileServer</address>\n");
        html.append("</body></html>");
        return html.toString();
    }

protected:
    const Config& getConfig() const { return config_; }
    
private:
    Config config_;

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
