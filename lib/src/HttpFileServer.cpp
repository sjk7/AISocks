// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpFileServer.h"

#include "FileIO.h"
#include "HtmlPageGenerator.h"
#include "PathHelper.h"

#include <array>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

namespace aiSocks {

HttpFileServer::HttpFileServer(
    const ServerBind& bind, const Config& config, Result<TcpSocket>* result)
    : HttpPollServer(bind, result) {
    config_.documentRoot
        = config.documentRoot.empty() ? "." : config.documentRoot;
    config_.indexFile
        = config.indexFile.empty() ? "index.html" : config.indexFile;
    config_.enableDirectoryListing = config.enableDirectoryListing;
    config_.enableETag = config.enableETag ? config.enableETag : true;
    config_.enableLastModified
        = config.enableLastModified ? config.enableLastModified : true;
    config_.maxFileSize
        = config.maxFileSize ? config.maxFileSize : 100 * 1024 * 1024;
    config_.enableCache = config.enableCache;
    config_.enableSecurityHeaders
        = config.enableSecurityHeaders ? config.enableSecurityHeaders : true;
    config_.hideServerVersion
        = config.hideServerVersion ? config.hideServerVersion : true;
    config_.customHeaders = config.customHeaders;

    if (!config_.documentRoot.empty() && config_.documentRoot.back() != '/') {
        config_.documentRoot += '/';
    }
}

void HttpFileServer::buildResponse(HttpClientState& state) {
    auto request = HttpRequest::parse(state.request);

    if (!request.valid) {
        sendError(state, 400, "Bad Request", "Invalid HTTP request");
        return;
    }

    if (request.method != "GET" && request.method != "HEAD") {
        sendError(state, 405, "Method Not Allowed",
            "Only GET and HEAD methods are supported");
        return;
    }

    std::string filePath = resolveFilePath(request.path);

    if (!PathHelper::isPathWithin(filePath, config_.documentRoot)
        && filePath != config_.documentRoot) {
        sendError(state, 403, "Forbidden", "Access denied");
        return;
    }

    if (PathHelper::hasSymlinkComponentWithin(filePath, config_.documentRoot)) {
        sendError(state, 403, "Forbidden", "Symlinks are not allowed");
        return;
    }

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

std::string HttpFileServer::resolveFilePath(const std::string& target) const {
    std::string path = target;

    size_t queryPos = path.find('?');
    if (queryPos != std::string::npos) {
        path = path.substr(0, queryPos);
    }
    size_t fragmentPos = path.find('#');
    if (fragmentPos != std::string::npos) {
        path = path.substr(0, fragmentPos);
    }

    // Strip trailing slash so stat() works uniformly; root "/" becomes "".
    if (!path.empty() && path.back() == '/' && path.size() > 1) path.pop_back();

    // "/" maps to the document root directory itself.
    // handleDirectoryRequest will try config_.indexFile first, then listing.
    if (path == "/") return config_.documentRoot;

    return config_.documentRoot + path.substr(1);
}

bool HttpFileServer::isFileSizeAcceptable(
    const std::string& /*filePath*/, size_t fileSize) const {
    return fileSize <= config_.maxFileSize;
}

HttpFileServer::FileInfo HttpFileServer::getFileInfo(
    const std::string& filePath) const {
    FileInfo info;

    PathHelper::FileInfo pathInfo = PathHelper::getFileInfo(filePath);

    if (!pathInfo.exists) {
        return info;
    }

    info.exists = true;
    info.isDirectory = pathInfo.isDirectory;

    if (!info.isDirectory) {
        info.size = pathInfo.size;
        info.lastModified = pathInfo.lastModified;

        if (config_.enableETag) {
            StringBuilder etag(64);
            etag.appendFormat(
                "\"%zu-%ld\"", info.size, static_cast<long>(info.lastModified));
            info.etag = etag.toString();
        }
    }

    return info;
}

bool HttpFileServer::isAccessAllowed(
    const std::string& filePath, const FileInfo& fileInfo) const {
    if (!fileInfo.exists) return false;

    // Block dotfiles/hidden dirs; allow /.well-known/
    {
        std::string root = PathHelper::normalizePath(config_.documentRoot);
        std::string path = PathHelper::normalizePath(filePath);
        if (!root.empty() && root.back() != '/') root.push_back('/');
        if (path.size() >= root.size()
            && path.compare(0, root.size(), root) == 0) {
            const std::string rel = path.substr(root.size());
            size_t i = 0;
            bool firstComponent = true;
            while (i < rel.size()) {
                while (i < rel.size() && rel[i] == '/') ++i;
                if (i >= rel.size()) break;
                const size_t j = rel.find('/', i);
                const std::string comp = (j == std::string::npos)
                    ? rel.substr(i)
                    : rel.substr(i, j - i);
                if (!comp.empty() && comp[0] == '.') {
                    if (!(firstComponent && comp == ".well-known")) {
                        return false;
                    }
                }
                firstComponent = false;
                if (j == std::string::npos) break;
                i = j + 1;
            }
        }
    }

    return true;
}

void HttpFileServer::handleDirectoryRequest(HttpClientState& state,
    const std::string& dirPath, const HttpRequest& request) {
    std::string indexPath = dirPath + "/" + config_.indexFile;
    FileInfo indexInfo = getFileInfo(indexPath);

    if (indexInfo.exists && !indexInfo.isDirectory) {
        handleFileRequest(state, indexPath, indexInfo, request);
        return;
    }

    if (config_.enableDirectoryListing) {
        sendDirectoryListing(state, dirPath);
    } else {
        sendError(state, 403, "Forbidden", "Directory listing not allowed");
    }
}

void HttpFileServer::handleFileRequest(HttpClientState& state,
    const std::string& filePath, const FileInfo& fileInfo,
    const HttpRequest& request) {
    bool useCache
        = config_.enableCache && request.path.find('?') == std::string::npos;

    if (useCache) {
        const FileCache::CachedFile* cached
            = fileCache_.get(filePath, fileInfo.lastModified);
        if (cached) {
            sendCachedFile(state, filePath, *cached, fileInfo, request);
            return;
        }
    }

    File file(filePath.c_str(), "rb");
    if (!file.isOpen()) {
        sendError(state, 500, "Internal Server Error", "Failed to open file");
        return;
    }

    File::FileInfo fdInfo = file.getInfoFromDescriptor();
    if (!fdInfo.valid) {
        sendError(state, 500, "Internal Server Error",
            "Failed to get file information");
        return;
    }

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
        sendError(
            state, 403, "Forbidden", "Access to this file is not allowed");
        return;
    }

    if (config_.enableETag && !fileInfo.etag.empty()) {
        auto ifNoneMatch = request.headers.find("if-none-match");
        if (ifNoneMatch != request.headers.end()
            && ifNoneMatch->second == fileInfo.etag) {
            sendNotModified(state, fileInfo);
            return;
        }
    }

    if (config_.enableLastModified) {
        auto ifModifiedSince = request.headers.find("if-modified-since");
        if (ifModifiedSince != request.headers.end()) {
            StringBuilder lastModified(64);
            lastModified.append(
                FileServerUtils::formatHttpDate(fileInfo.lastModified));
            if (ifModifiedSince->second == lastModified.toString()) {
                sendNotModified(state, fileInfo);
                return;
            }
        }
    }

    std::vector<char> fileContent = file.readAll();
    if (fileContent.empty() && fdInfo.size > 0) {
        sendError(state, 500, "Internal Server Error", "Failed to read file");
        return;
    }

    if (useCache) {
        fileCache_.put(filePath, fileContent, fileInfo.lastModified);
    }

    StringBuilder response(512 + fileContent.size());
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: ");
    response.append(MimeTypes::fromPath(filePath));
    if (MimeTypes::fromPath(filePath).find("text/") == 0
        || MimeTypes::fromPath(filePath) == "application/javascript") {
        response.append("; charset=utf-8");
    }
    response.append("\r\nContent-Length: ");
    response.appendFormat("%zu", fileContent.size());
    response.append("\r\n");

    if (config_.enableLastModified) {
        response.append("Last-Modified: ");
        response.append(FileServerUtils::formatHttpDate(fileInfo.lastModified));
        response.append("\r\n");
    }

    if (config_.enableETag && !fileInfo.etag.empty()) {
        response.append("ETag: ");
        response.append(fileInfo.etag);
        response.append("\r\n");
    }

    if (config_.enableSecurityHeaders)
        response.append(FileServerUtils::securityHeadersBlock());

    for (const auto& [name, value] : config_.customHeaders) {
        response.append(name);
        response.append(": ");
        response.append(value);
        response.append("\r\n");
    }

    response.append("\r\n");

    state.responseBuf = response.toString()
        + std::string(fileContent.begin(), fileContent.end());
    state.responseView = state.responseBuf;
}

void HttpFileServer::sendError(HttpClientState& state, int code,
    const std::string& status, const std::string& message) {
    // Note: message is HTML-escaped in generateErrorHtml(), so it's safe to
    // include user input (e.g., request paths) in error messages
    std::string htmlBody = generateErrorHtml(code, status, message);

    StringBuilder response(256 + htmlBody.size());
    response.append("HTTP/1.1 ");
    response.appendFormat("%d", code);
    response.append(" ");
    response.append(status);
    response.append(
        "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ");
    response.appendFormat("%zu", htmlBody.size());
    response.append("\r\n");

    if (config_.enableSecurityHeaders)
        response.append(FileServerUtils::securityHeadersBlock());

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

void HttpFileServer::sendNotModified(
    HttpClientState& state, const FileInfo& fileInfo) {
    StringBuilder response(256);
    response.append("HTTP/1.1 304 Not Modified\r\n");

    if (config_.enableLastModified) {
        response.append("Last-Modified: ");
        response.append(FileServerUtils::formatHttpDate(fileInfo.lastModified));
        response.append("\r\n");
    }

    if (config_.enableETag && !fileInfo.etag.empty()) {
        response.append("ETag: ");
        response.append(fileInfo.etag);
        response.append("\r\n");
    }

    if (config_.enableSecurityHeaders)
        response.append(FileServerUtils::securityHeadersBlock());

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

void HttpFileServer::sendCachedFile(HttpClientState& state,
    const std::string& filePath, const FileCache::CachedFile& cached,
    const FileInfo& fileInfo, const HttpRequest& request) {
    (void)request;

    StringBuilder response(512 + cached.size);
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: ");
    response.append(MimeTypes::fromPath(filePath));
    if (MimeTypes::fromPath(filePath).find("text/") == 0
        || MimeTypes::fromPath(filePath) == "application/javascript") {
        response.append("; charset=utf-8");
    }
    response.append("\r\nContent-Length: ");
    response.appendFormat("%zu", cached.size);
    response.append("\r\n");

    if (config_.enableLastModified) {
        response.append("Last-Modified: ");
        response.append(FileServerUtils::formatHttpDate(fileInfo.lastModified));
        response.append("\r\n");
    }

    if (config_.enableETag && !fileInfo.etag.empty()) {
        response.append("ETag: ");
        response.append(fileInfo.etag);
        response.append("\r\n");
    }

    if (config_.enableSecurityHeaders)
        response.append(FileServerUtils::securityHeadersBlock());

    for (const auto& [name, value] : config_.customHeaders) {
        response.append(name);
        response.append(": ");
        response.append(value);
        response.append("\r\n");
    }

    response.append("\r\n");

    state.responseBuf = response.toString()
        + std::string(cached.content.begin(), cached.content.end());
    state.responseView = state.responseBuf;
}

void HttpFileServer::sendDirectoryListing(
    HttpClientState& state, const std::string& dirPath) {
    std::string htmlBody = generateDirectoryListing(dirPath);

    StringBuilder response(256 + htmlBody.size());
    response.append("HTTP/1.1 200 OK\r\n");
    response.append(
        "Content-Type: text/html; charset=utf-8\r\nContent-Length: ");
    response.appendFormat("%zu", htmlBody.size());
    response.append("\r\n");

    if (config_.enableSecurityHeaders)
        response.append(FileServerUtils::securityHeadersBlock());

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

std::string HttpFileServer::generateErrorHtml(
    int code, const std::string& status, const std::string& message) const {
    return HtmlPageGenerator(config_.hideServerVersion)
        .errorPage(code, status, message);
}

std::string HttpFileServer::generateDirectoryListing(
    const std::string& dirPath) const {
    return HtmlPageGenerator(config_.hideServerVersion)
        .directoryListing(dirPath);
}

const HttpFileServer::Config& HttpFileServer::getConfig() const {
    return config_;
}

FileCache& HttpFileServer::getFileCache() {
    return fileCache_;
}

} // namespace aiSocks
