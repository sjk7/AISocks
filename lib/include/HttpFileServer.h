// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include "HttpPollServer.h"
#include "HttpRequest.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <iomanip>
#include <filesystem>

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

        // Security check: prevent path traversal
        if (request.path.find("..") != std::string::npos) {
            sendError(state, 400, "Bad Request", "Path traversal not allowed");
            return;
        }

        // Resolve the file path
        std::string filePath = resolveFilePath(request.path);
        
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
        std::filesystem::file_time_type lastModified;
        std::string etag;
    };

    /// Virtual customization point: get file information
    virtual FileInfo getFileInfo(const std::string& filePath) const {
        FileInfo info;
        
        try {
            std::filesystem::path path(filePath);
            
            if (!std::filesystem::exists(path)) {
                return info; // exists = false
            }

            info.exists = true;
            info.isDirectory = std::filesystem::is_directory(path);
            
            if (!info.isDirectory) {
                info.size = std::filesystem::file_size(path);
                info.lastModified = std::filesystem::last_write_time(path);
                
                // Generate ETag based on size and modification time
                if (config_.enableETag) {
                    std::ostringstream etag;
                    etag << "\"" << info.size << "-" 
                         << std::chrono::duration_cast<std::chrono::seconds>(
                                info.lastModified.time_since_epoch()).count() << "\"";
                    info.etag = etag.str();
                }
            }
        } catch (const std::exception&) {
            // Return default info (exists = false) on any error
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
        if (!isAccessAllowed(filePath, fileInfo)) {
            sendError(state, 403, "Forbidden", "Access to this file is not allowed");
            return;
        }

        // Check conditional headers
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
                // Simple string comparison for If-Modified-Since
                // In production, you'd want proper date parsing
                std::ostringstream lastModified;
                lastModified << formatHttpDate(fileInfo.lastModified);
                if (ifModifiedSince->second == lastModified.str()) {
                    sendNotModified(state, fileInfo);
                    return;
                }
            }
        }

        // Read and send file
        std::vector<char> fileContent = readFileContent(filePath);
        if (fileContent.empty()) {
            sendError(state, 500, "Internal Server Error", "Failed to read file");
            return;
        }

        // Build response
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: " << getMimeType(filePath) << "\r\n";
        response << "Content-Length: " << fileContent.size() << "\r\n";
        
        if (config_.enableLastModified) {
            response << "Last-Modified: " << formatHttpDate(fileInfo.lastModified) << "\r\n";
        }
        
        if (config_.enableETag && !fileInfo.etag.empty()) {
            response << "ETag: " << fileInfo.etag << "\r\n";
        }
        
        // Add custom headers
        for (const auto& [name, value] : config_.customHeaders) {
            response << name << ": " << value << "\r\n";
        }
        
        response << "\r\n";
        
        std::string headerStr = response.str();
        state.responseBuf = headerStr + std::string(fileContent.begin(), fileContent.end());
        state.responseView = state.responseBuf;
    }

    /// Virtual customization point: send error response
    virtual void sendError(HttpClientState& state, int code, const std::string& status, const std::string& message) {
        std::string htmlBody = generateErrorHtml(code, status, message);
        
        std::ostringstream response;
        response << "HTTP/1.1 " << code << " " << status << "\r\n";
        response << "Content-Type: text/html\r\n";
        response << "Content-Length: " << htmlBody.size() << "\r\n";
        
        // Add custom headers
        for (const auto& [name, value] : config_.customHeaders) {
            response << name << ": " << value << "\r\n";
        }
        
        response << "\r\n" << htmlBody;
        
        state.responseBuf = response.str();
        state.responseView = state.responseBuf;
    }

    /// Virtual customization point: send 304 Not Modified response
    virtual void sendNotModified(HttpClientState& state, const FileInfo& fileInfo) {
        std::ostringstream response;
        response << "HTTP/1.1 304 Not Modified\r\n";
        
        if (config_.enableLastModified) {
            response << "Last-Modified: " << formatHttpDate(fileInfo.lastModified) << "\r\n";
        }
        
        if (config_.enableETag && !fileInfo.etag.empty()) {
            response << "ETag: " << fileInfo.etag << "\r\n";
        }
        
        // Add custom headers
        for (const auto& [name, value] : config_.customHeaders) {
            response << name << ": " << value << "\r\n";
        }
        
        response << "\r\n";
        
        state.responseBuf = response.str();
        state.responseView = state.responseBuf;
    }

    /// Virtual customization point: send directory listing
    virtual void sendDirectoryListing(HttpClientState& state, const std::string& dirPath) {
        std::string htmlBody = generateDirectoryListing(dirPath);
        
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: text/html\r\n";
        response << "Content-Length: " << htmlBody.size() << "\r\n";
        
        // Add custom headers
        for (const auto& [name, value] : config_.customHeaders) {
            response << name << ": " << value << "\r\n";
        }
        
        response << "\r\n" << htmlBody;
        
        state.responseBuf = response.str();
        state.responseView = state.responseBuf;
    }

    /// Virtual customization point: generate error HTML
    virtual std::string generateErrorHtml(int code, const std::string& status, const std::string& message) const {
        std::ostringstream html;
        html << "<!DOCTYPE html>\n";
        html << "<html><head><title>" << code << " " << status << "</title></head>\n";
        html << "<body><h1>" << code << " " << status << "</h1>\n";
        html << "<p>" << message << "</p>\n";
        html << "<hr><address>aiSocks HttpFileServer</address>\n";
        html << "</body></html>";
        return html.str();
    }

    /// Virtual customization point: generate directory listing HTML
    virtual std::string generateDirectoryListing(const std::string& dirPath) const {
        std::ostringstream html;
        html << "<!DOCTYPE html>\n";
        html << "<html><head><title>Directory listing for " << dirPath << "</title></head>\n";
        html << "<body><h1>Directory listing for " << dirPath << "</h1>\n";
        html << "<ul>\n";
        
        try {
            for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                std::string name = entry.path().filename().string();
                if (name.empty() || name[0] == '.') continue; // Skip hidden files
                
                std::string path = entry.path().string();
                bool isDir = entry.is_directory();
                
                html << "<li><a href=\"" << name << (isDir ? "/" : "") << "\">";
                html << name << (isDir ? "/" : "") << "</a></li>\n";
            }
        } catch (const std::exception&) {
            html << "<li>Error reading directory</li>\n";
        }
        
        html << "</ul>\n";
        html << "<hr><address>aiSocks HttpFileServer</address>\n";
        html << "</body></html>";
        return html.str();
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

    std::string urlDecode(const std::string& encoded) const {
        std::string decoded;
        for (size_t i = 0; i < encoded.length(); ++i) {
            if (encoded[i] == '%' && i + 2 < encoded.length()) {
                int hex;
                std::istringstream hexStream(encoded.substr(i + 1, 2));
                if (hexStream >> std::hex >> hex) {
                    decoded += static_cast<char>(hex);
                    i += 2;
                } else {
                    decoded += encoded[i];
                }
            } else if (encoded[i] == '+') {
                decoded += ' ';
            } else {
                decoded += encoded[i];
            }
        }
        return decoded;
    }

    std::vector<char> readFileContent(const std::string& filePath) const {
        std::ifstream file(filePath, std::ios::binary);
        if (!file) return {};
        
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::vector<char> content(size);
        if (!file.read(content.data(), size)) {
            return {};
        }
        
        return content;
    }

    std::string formatHttpDate(const std::filesystem::file_time_type& fileTime) const {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
        
        std::ostringstream oss;
#ifdef _WIN32
        struct tm timeinfo = {};
        gmtime_s(&timeinfo, &cftime);
        oss << std::put_time(&timeinfo, "%a, %d %b %Y %H:%M:%S GMT");
#else
        oss << std::put_time(std::gmtime(&cftime), "%a, %d %b %Y %H:%M:%S GMT");
#endif
        return oss.str();
    }
};

} // namespace aiSocks
