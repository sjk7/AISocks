// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com



#pragma once

#include "FileCache.h"
#include "HttpPollServer.h"
#include "HttpRequest.h"
#include <ctime>
#include <map>
#include <string>

namespace aiSocks {

// Forward declaration — addSecurityHeaders takes StringBuilder& but
// StringBuilder is defined in FileIO.h, which we avoid pulling into every
// including TU (it drags in platform headers).
class StringBuilder;

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
        std::string documentRoot; // Root directory for serving files
        std::string indexFile; // Default file for directory requests
        bool enableDirectoryListing
            = false; // Show directory contents when no index file
        bool enableETag = false; // Generate ETag headers for files
        bool enableLastModified = false; // Generate Last-Modified headers
        size_t maxFileSize = 0; // Max file size to serve (100MB default)
        bool enableCache = false; // Enable file content caching
        bool enableSecurityHeaders
            = false; // Add security headers (X-Content-Type-Options, etc.)
        bool hideServerVersion = false; // Hide server version in error pages
        std::map<std::string, std::string>
            customHeaders; // Additional headers to add
    };

    explicit HttpFileServer(const ServerBind& bind, const Config& config,
        Result<TcpSocket>* result = nullptr);

    protected:
    /// Main request handler - routes to appropriate handlers
    void buildResponse(HttpClientState& state) override;

    /// Virtual customization point: resolve file path from request target
    virtual std::string resolveFilePath(const std::string& target) const;

    /// File information structure
    struct FileInfo {
        bool exists = false;
        bool isDirectory = false;
        size_t size = 0;
        time_t lastModified = 0;
        std::string etag;
    };

    /// Virtual customization point: validate file size
    virtual bool isFileSizeAcceptable(
        const std::string& filePath, size_t fileSize) const;

    /// Virtual customization point: get file information
    virtual FileInfo getFileInfo(const std::string& filePath) const;

    /// Virtual customization point: get MIME type for file
    virtual std::string getMimeType(const std::string& filePath) const;

    /// Virtual customization point: check if file access is allowed
    virtual bool isAccessAllowed(
        const std::string& filePath, const FileInfo& fileInfo) const;

    /// Virtual customization point: handle directory requests
    virtual void handleDirectoryRequest(HttpClientState& state,
        const std::string& dirPath, const HttpRequest& request);

    /// Virtual customization point: handle file requests
    virtual void handleFileRequest(HttpClientState& state,
        const std::string& filePath, const FileInfo& fileInfo,
        const HttpRequest& request);

    /// Virtual customization point: send error response
    virtual void sendError(HttpClientState& state, int code,
        const std::string& status, const std::string& message);

    /// Virtual customization point: send 304 Not Modified response
    virtual void sendNotModified(
        HttpClientState& state, const FileInfo& fileInfo);

    /// Send cached file content
    void sendCachedFile(HttpClientState& state, const std::string& filePath,
        const FileCache::CachedFile& cached, const FileInfo& fileInfo,
        const HttpRequest& request);

    /// Virtual customization point: send directory listing
    virtual void sendDirectoryListing(
        HttpClientState& state, const std::string& dirPath);

    /// Virtual customization point: generate error HTML
    virtual std::string generateErrorHtml(
        int code, const std::string& status, const std::string& message) const;

    /// Virtual customization point: generate directory listing HTML
    virtual std::string generateDirectoryListing(
        const std::string& dirPath) const;

    const Config& getConfig() const;
    FileCache& getFileCache();

    private:
    Config config_;
    FileCache fileCache_;

    void addSecurityHeaders(StringBuilder& response) const;
    static std::string htmlEscape(const std::string& str);
    static std::string urlDecodePath(const std::string& src);
    std::string getFileExtension(const std::string& filePath) const;
    std::string formatHttpDate(time_t fileTime) const;
};

} // namespace aiSocks
