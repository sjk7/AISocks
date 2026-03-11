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

// ---------------------------------------------------------------------------
// Translation-unit helpers — avoid repeating the same header-assembly pattern
// in every send* function.
// ---------------------------------------------------------------------------
namespace {

    // Append security headers (if enabled) and all custom headers, then the
    // blank line that terminates the HTTP header section.
    void appendConfigAndTrailingCRLF(std::string& response,
        bool enableSecurityHeaders,
        const std::map<std::string, std::string>& customHeaders) {
        if (enableSecurityHeaders)
            response += FileServerUtils::securityHeadersBlock();
        for (const auto& [name, value] : customHeaders) {
            response += name;
            response += ": ";
            response += value;
            response += "\r\n";
        }
        response += "\r\n";
    }

    // Append a complete HTTP/1.1 200 OK header block (including the final blank
    // line).  Content is NOT appended; the caller does that.
    // lastModified and etag are raw values from FileInfo; cfg drives which
    // optional headers to emit and which config-level headers to append.
    void appendOkHeaders(std::string& response, const std::string& filePath,
        size_t contentSize, time_t lastModified, const std::string& etag,
        const HttpFileServer::Config& cfg) {
        response += "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: ";
        const std::string mime = MimeTypes::fromPath(filePath);
        response += mime;
        if (mime.find("text/") == 0 || mime == "application/javascript")
            response += "; charset=utf-8";
        response += "\r\nContent-Length: ";
        response += std::to_string(contentSize);
        response += "\r\n";
        if (cfg.enableLastModified) {
            response += "Last-Modified: ";
            response += FileServerUtils::formatHttpDate(lastModified);
            response += "\r\n";
        }
        if (cfg.enableETag && !etag.empty()) {
            response += "ETag: ";
            response += etag;
            response += "\r\n";
        }
        appendConfigAndTrailingCRLF(
            response, cfg.enableSecurityHeaders, cfg.customHeaders);
    }

} // anonymous namespace

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

bool HttpFileServer::validateFilePath_(
    HttpClientState& state, const std::string& filePath) {
    if (!PathHelper::isPathWithin(filePath, config_.documentRoot)
        && filePath != config_.documentRoot) {
        sendError(state, 403, "Forbidden", "Access denied");
        return false;
    }
    if (PathHelper::hasSymlinkComponentWithin(filePath, config_.documentRoot)) {
        sendError(state, 403, "Forbidden", "Symlinks are not allowed");
        return false;
    }
    return true;
}

void HttpFileServer::buildResponse(HttpClientState& state) {
    // Use the pre-parsed request stored by dispatchBuildResponse; fall back to
    // parsing here only when buildResponse is called via a non-standard path.
    HttpRequest request = state.parsedRequest.valid
        ? std::move(state.parsedRequest)
        : HttpRequest::parse(state.request);
    state.parsedRequest = HttpRequest{};

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

    if (!validateFilePath_(state, filePath)) return;

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

    // Strip trailing slash so stat() works uniformly; root "/" becomes "".
    if (!path.empty() && path.back() == '/' && path.size() > 1) path.pop_back();

    // "/" or empty maps to the document root directory itself.
    // handleDirectoryRequest will try config_.indexFile first, then listing.
    if (path.empty() || path == "/") return config_.documentRoot;

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
            char etagBuf[64];
            snprintf(etagBuf, sizeof(etagBuf), "\"%zu-%ld\"", info.size,
                static_cast<long>(info.lastModified));
            info.etag = etagBuf;
        }
    }

    return info;
}

// Returns true if the relative path `rel` contains any dotfile component that
// should be blocked.  The first component ".well-known" is exempt (RFC 8615).
static bool hasDotfileComponent_(std::string_view rel) {
    size_t i = 0;
    bool firstComponent = true;
    while (i < rel.size()) {
        while (i < rel.size() && rel[i] == '/') ++i;
        if (i >= rel.size()) break;
        const size_t j = rel.find('/', i);
        const std::string_view comp = (j == std::string_view::npos)
            ? rel.substr(i)
            : rel.substr(i, j - i);
        if (!comp.empty() && comp[0] == '.') {
            if (!(firstComponent && comp == ".well-known")) return true;
        }
        firstComponent = false;
        if (j == std::string_view::npos) break;
        i = j + 1;
    }
    return false;
}

bool HttpFileServer::isAccessAllowed(
    const std::string& filePath, const FileInfo& fileInfo) const {
    if (!fileInfo.exists) return false;

    // Block dotfiles/hidden dirs; allow /.well-known/
    std::string root = PathHelper::normalizePath(config_.documentRoot);
    std::string path = PathHelper::normalizePath(filePath);
    if (!root.empty() && root.back() != '/') root.push_back('/');
    if (path.size() >= root.size() && path.compare(0, root.size(), root) == 0) {
        if (hasDotfileComponent_(path.substr(root.size()))) return false;
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

bool HttpFileServer::checkCacheConditions_(HttpClientState& state,
    const HttpRequest& request, const FileInfo& fileInfo) {
    if (config_.enableETag && !fileInfo.etag.empty()) {
        auto it = request.headers.find("if-none-match");
        if (it != request.headers.end() && it->second == fileInfo.etag) {
            sendNotModified(state, fileInfo);
            return true;
        }
    }
    if (config_.enableLastModified) {
        auto it = request.headers.find("if-modified-since");
        if (it != request.headers.end()) {
            if (it->second
                == FileServerUtils::formatHttpDate(fileInfo.lastModified)) {
                sendNotModified(state, fileInfo);
                return true;
            }
        }
    }
    return false;
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

    if (checkCacheConditions_(state, request, fileInfo)) return;

    std::vector<char> fileContent = file.readAll();
    if (fileContent.empty() && fdInfo.size > 0) {
        sendError(state, 500, "Internal Server Error", "Failed to read file");
        return;
    }

    if (useCache) {
        fileCache_.put(filePath, fileContent, fileInfo.lastModified);
    }

    std::string response;
    response.reserve(512 + fileContent.size());
    appendOkHeaders(response, filePath, fileContent.size(),
        fileInfo.lastModified, fileInfo.etag, config_);
    response.append(fileContent.data(), fileContent.size());

    state.responseBuf = std::move(response);
    state.responseView = state.responseBuf;
}

void HttpFileServer::sendError(HttpClientState& state, int code,
    const std::string& status, const std::string& message) {
    // Note: message is HTML-escaped in generateErrorHtml(), so it's safe to
    // include user input (e.g., request paths) in error messages
    std::string htmlBody = generateErrorHtml(code, status, message);

    std::string response;
    response.reserve(256 + htmlBody.size());
    response += "HTTP/1.1 ";
    response += std::to_string(code);
    response += ' ';
    response += status;
    response
        += "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ";
    response += std::to_string(htmlBody.size());
    response += "\r\n";

    appendConfigAndTrailingCRLF(
        response, config_.enableSecurityHeaders, config_.customHeaders);
    response += htmlBody;

    state.responseBuf = std::move(response);
    state.responseView = state.responseBuf;
}

void HttpFileServer::sendNotModified(
    HttpClientState& state, const FileInfo& fileInfo) {
    std::string response;
    response.reserve(256);
    response += "HTTP/1.1 304 Not Modified\r\n";

    if (config_.enableLastModified) {
        response += "Last-Modified: ";
        response += FileServerUtils::formatHttpDate(fileInfo.lastModified);
        response += "\r\n";
    }

    if (config_.enableETag && !fileInfo.etag.empty()) {
        response += "ETag: ";
        response += fileInfo.etag;
        response += "\r\n";
    }

    appendConfigAndTrailingCRLF(
        response, config_.enableSecurityHeaders, config_.customHeaders);

    state.responseBuf = std::move(response);
    state.responseView = state.responseBuf;
}

void HttpFileServer::sendCachedFile(HttpClientState& state,
    const std::string& filePath, const FileCache::CachedFile& cached,
    const FileInfo& fileInfo, const HttpRequest& request) {
    (void)request;

    std::string response;
    response.reserve(512 + cached.size);
    appendOkHeaders(response, filePath, cached.size, fileInfo.lastModified,
        fileInfo.etag, config_);
    response.append(cached.content.data(), cached.content.size());

    state.responseBuf = std::move(response);
    state.responseView = state.responseBuf;
}

void HttpFileServer::sendDirectoryListing(
    HttpClientState& state, const std::string& dirPath) {
    std::string htmlBody = generateDirectoryListing(dirPath);

    std::string response;
    response.reserve(256 + htmlBody.size());
    response += "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: text/html; charset=utf-8\r\nContent-Length: ";
    response += std::to_string(htmlBody.size());
    response += "\r\n";

    appendConfigAndTrailingCRLF(
        response, config_.enableSecurityHeaders, config_.customHeaders);
    response += htmlBody;

    state.responseBuf = std::move(response);
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
