// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "HttpFileServer.h"

#include "FileIO.h"
#include "HtmlPageGenerator.h"
#include "PathHelper.h"
#include "Socket.h"

#include <array>
#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>

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

    // Large responses use a direct-read path that fills the final response
    // buffer in place, avoiding an extra full-file temporary allocation.
    constexpr size_t kLargeDirectReadThresholdBytes = 256 * 1024;

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
        
        // Add cache-control headers for JS and HTML files to prevent browser caching
        if ((filePath.size() >= 3 && filePath.substr(filePath.size() - 3) == ".js") ||
            (filePath.size() >= 4 && filePath.substr(filePath.size() - 4) == ".htm") ||
            (filePath.size() >= 5 && filePath.substr(filePath.size() - 5) == ".html") ||
            (filePath.size() >= 6 && filePath.substr(filePath.size() - 6) == ".mjs")) {
            response += "Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n";
            response += "Pragma: no-cache\r\n";
            response += "Expires: 0\r\n";
        }
        
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

    // For HEAD, keep headers (including Content-Length) but drop the body.
    void stripBodyForHead_(HttpClientState& state) {
        const size_t sep = state.dataBuf.find("\r\n\r\n");
        if (sep == std::string::npos) return;
        state.dataBuf.resize(sep + 4);
        state.dataView = state.dataBuf;
    }

} // anonymous namespace

HttpFileServer::HttpFileServer(
    const ServerBind& bind, const Config& config, Result<TcpSocket>* result)
    : HttpPollServer(bind, result)
    , bind_(bind)
    , logRotation_(config.logPath, config.logRotation) {
    config_.documentRoot
        = config.documentRoot.empty() ? "." : config.documentRoot;
    config_.indexFile
        = config.indexFile.empty() ? "index.html" : config.indexFile;
    config_.enableDirectoryListing = config.enableDirectoryListing;
    config_.enableETag = config.enableETag;
    config_.enableLastModified = config.enableLastModified;
    config_.maxFileSize
        = config.maxFileSize ? config.maxFileSize : 100 * 1024 * 1024;
    config_.enableCache = config.enableCache;
    config_.enableSecurityHeaders = config.enableSecurityHeaders;
    config_.hideServerVersion = config.hideServerVersion;
    config_.customHeaders = config.customHeaders;
    config_.logPath = config.logPath;
    config_.enableLogging = config.enableLogging;
    config_.logRotation = config.logRotation;

    if (!config_.documentRoot.empty() && config_.documentRoot.back() != '/') {
        config_.documentRoot += '/';
    }
    
    // Open log file if logging is enabled
    if (config_.enableLogging) {
        logFile_.open(config_.logPath.c_str(), "a");
    }
    
    // Set up rotation callback to call virtual onLogRotate method
    logRotation_.setCallback([this](const std::string& rotatedPath) {
        onLogRotate(rotatedPath);
    });
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

    // Log the request
    logRequest(request, state);

    if (request.path == "/api/config/save" && request.method == "POST") {
        if (!isLocalClient(state.peerAddress)) {
            sendError(state, 403, "Forbidden", "Config API is local only");
            return;
        }
        handleSaveConfig(state, request);
        return;
    }

    // Config editor API endpoints (local only)
    if (request.path == "/api/config/ips" && request.method == "GET") {
        if (!isLocalClient(state.peerAddress)) {
            sendError(state, 403, "Forbidden", "Config API is local only");
            return;
        }
        handleGetAvailableIPs(state);
        return;
    }

    if (request.path == "/api/config/current" && request.method == "GET") {
        if (!isLocalClient(state.peerAddress)) {
            sendError(state, 403, "Forbidden", "Config API is local only");
            return;
        }
        handleGetCurrentConfig(state);
        return;
    }

    if (request.method != "GET" && request.method != "HEAD" && request.method != "POST") {
        sendError(state, 405, "Method Not Allowed",
            "Only GET, HEAD, and POST methods are supported");
        return;
    }

    const bool headOnly = (request.method == "HEAD");

    std::string filePath = resolveFilePath(request.path);

    if (!validateFilePath_(state, filePath)) return;

    FileInfo fileInfo = getFileInfo(filePath);

    if (!fileInfo.exists) {
        sendError(state, 404, "Not Found", "File not found");
        if (headOnly) stripBodyForHead_(state);
        return;
    }

    if (fileInfo.isDirectory) {
        handleDirectoryRequest(state, filePath, request);
    } else {
        handleFileRequest(state, filePath, fileInfo, request);
    }

    if (headOnly) stripBodyForHead_(state);
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
    const bool cacheEligible = config_.enableCache
        && request.queryString.empty()
        && fileInfo.size <= fileCache_.getConfig().maxFileSize
        && fileInfo.size < kLargeDirectReadThresholdBytes;

    if (cacheEligible) {
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

    if (!file.seek(0, SEEK_SET)) {
        sendError(state, 500, "Internal Server Error", "Failed to read file");
        return;
    }

    if (!cacheEligible && fdInfo.size >= kLargeDirectReadThresholdBytes) {
        std::string response;
        response.reserve(512);
        appendOkHeaders(response, filePath, fdInfo.size, fileInfo.lastModified,
            fileInfo.etag, config_);

        const bool headOnly = (request.method == "HEAD");
        if (headOnly) {
            state.dataBuf = std::move(response);
            state.dataView = state.dataBuf;
            return;
        }

        auto streamFile = std::make_shared<File>(std::move(file));
        if (!streamFile || !streamFile->isOpen()) {
            sendError(
                state, 500, "Internal Server Error", "Failed to read file");
            return;
        }

        setStreamedFileResponse(
            state, std::move(response), std::move(streamFile), fdInfo.size);
        return;
    }

    std::vector<char> fileContent = file.readAll();
    if (fileContent.empty() && fdInfo.size > 0) {
        sendError(state, 500, "Internal Server Error", "Failed to read file");
        return;
    }

    if (cacheEligible)
        fileCache_.put(filePath, fileContent, fileInfo.lastModified);

    std::string response;
    response.reserve(512 + fileContent.size());
    appendOkHeaders(response, filePath, fileContent.size(),
        fileInfo.lastModified, fileInfo.etag, config_);
    response.append(fileContent.data(), fileContent.size());

    state.dataBuf = std::move(response);
    state.dataView = state.dataBuf;
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

    state.dataBuf = std::move(response);
    state.dataView = state.dataBuf;
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

    state.dataBuf = std::move(response);
    state.dataView = state.dataBuf;
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

    state.dataBuf = std::move(response);
    state.dataView = state.dataBuf;
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

    state.dataBuf = std::move(response);
    state.dataView = state.dataBuf;
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

void HttpFileServer::onLogRotate(const std::string& rotatedFilePath) {
    // Default implementation does nothing - override in derived classes
    (void)rotatedFilePath;
}

bool HttpFileServer::isLocalClient(const std::string& peerAddress) const {
    // Check if address is loopback (127.0.0.0/8 or ::1)
    if (peerAddress.find("127.") == 0 || peerAddress == "::1" || peerAddress == "localhost") {
        return true;
    }
    return false;
}

void HttpFileServer::handleGetAvailableIPs(HttpClientState& state) {
    // Get available IP addresses from the library
    std::vector<NetworkInterface> interfaces = Socket::getLocalAddresses();
    
    std::string json = R"({"ips": [)";
    bool first = true;
    for (const auto& iface : interfaces) {
        if (!first) json += ", ";
        json += "\"" + iface.address + "\"";
        first = false;
    }
    json += "]}";
    
    std::string response;
    response.reserve(256 + json.size());
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: application/json\r\nContent-Length: ");
    response += std::to_string(json.size());
    response.append("\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
    response.append(json);
    
    state.dataBuf = std::move(response);
    state.dataView = state.dataBuf;
}

void HttpFileServer::handleGetCurrentConfig(HttpClientState& state) {
    // Return current HttpFileServer::Config as JSON
    std::string json = "{";
    json += "\"bindAddress\": \"" + bind_.address + "\", ";
    json += "\"httpPort\": " + std::to_string(bind_.port.value()) + ", ";
    json += "\"wwwRoot\": \"" + config_.documentRoot + "\", ";
    json += "\"indexFile\": \"" + config_.indexFile + "\", ";
    json += "\"enableDirectoryListing\": " + (config_.enableDirectoryListing ? std::string("true") : std::string("false")) + ", ";
    json += "\"enableETag\": " + (config_.enableETag ? std::string("true") : std::string("false")) + ", ";
    json += "\"enableLastModified\": " + (config_.enableLastModified ? std::string("true") : std::string("false")) + ", ";
    json += "\"maxFileSize\": " + std::to_string(config_.maxFileSize) + ", ";
    json += "\"enableCache\": " + (config_.enableCache ? std::string("true") : std::string("false")) + ", ";
    json += "\"enableSecurityHeaders\": " + (config_.enableSecurityHeaders ? std::string("true") : std::string("false")) + ", ";
    json += "\"hideServerVersion\": " + (config_.hideServerVersion ? std::string("true") : std::string("false")) + ", ";
    json += "\"logPath\": \"" + config_.logPath + "\", ";
    json += "\"enableLogging\": " + (config_.enableLogging ? std::string("true") : std::string("false")) + ", ";
    json += "\"enableLogRotation\": " + (config_.logRotation.enabled ? std::string("true") : std::string("false")) + ", ";
    json += "\"logMaxSizeBytes\": " + std::to_string(config_.logRotation.maxSizeBytes) + ", ";
    json += "\"logMaxFiles\": " + std::to_string(config_.logRotation.maxFiles) + ", ";
    
    // Read HTTPS settings from server.conf if present
    bool enableHttps = false;
    int httpsPort = 8443;
    std::string cert = "server-cert.pem";
    std::string key = "server-key.pem";
    bool directoryListing = true; // default
    size_t logMaxSize = config_.logRotation.maxSizeBytes; // default to in-memory value
    
    std::ifstream confFile("server.conf");
    if (confFile.is_open()) {
        std::string line;
        while (std::getline(confFile, line)) {
            if (line.find("enable_https=") == 0) {
                enableHttps = (line.find("true") != std::string::npos);
            } else if (line.find("https_port=") == 0) {
                size_t pos = line.find('=');
                if (pos != std::string::npos) {
                    httpsPort = std::stoi(line.substr(pos + 1));
                }
            } else if (line.find("cert=") == 0) {
                size_t pos = line.find('=');
                if (pos != std::string::npos) {
                    cert = line.substr(pos + 1);
                }
            } else if (line.find("key=") == 0) {
                size_t pos = line.find('=');
                if (pos != std::string::npos) {
                    key = line.substr(pos + 1);
                }
            } else if (line.find("directory_listing=") == 0) {
                directoryListing = (line.find("true") != std::string::npos);
            } else if (line.find("log_max_size=") == 0) {
                size_t pos = line.find('=');
                if (pos != std::string::npos) {
                    size_t sizeMB = std::stoull(line.substr(pos + 1));
                    logMaxSize = (sizeMB == 0) ? (100 * 1024 * 1024) : (sizeMB * 1024 * 1024); // 0 = 100MB
                }
            }
        }
        confFile.close();
    }
    
    json += "\"enableHttps\": " + (enableHttps ? std::string("true") : std::string("false")) + ", ";
    json += "\"httpsPort\": " + std::to_string(httpsPort) + ", ";
    json += "\"cert\": \"" + cert + "\", ";
    json += "\"key\": \"" + key + "\", ";
    json += "\"enableDirectoryListing\": " + (directoryListing ? std::string("true") : std::string("false")) + ", ";
    json += "\"logMaxSizeBytes\": " + std::to_string(logMaxSize);
    json += "}";
    
    std::string response;
    response.reserve(256 + json.size());
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: application/json\r\nContent-Length: ");
    response += std::to_string(json.size());
    response.append("\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
    response.append(json);
    
    state.dataBuf = std::move(response);
    state.dataView = state.dataBuf;
}

void HttpFileServer::handleSaveConfig(HttpClientState& state, const HttpRequest& request) {
    (void)request; // Unused for now
    // Parse JSON body from request
    std::string jsonBody;
    const std::string bodyPrefix = "Content-Length: ";
    size_t contentLengthPos = state.request.find(bodyPrefix);
    if (contentLengthPos != std::string::npos) {
        size_t endPos = state.request.find("\r\n", contentLengthPos);
        if (endPos != std::string::npos) {
            std::string lengthStr = state.request.substr(contentLengthPos + bodyPrefix.length(), endPos - contentLengthPos - bodyPrefix.length());
            size_t contentLength = std::stoul(lengthStr);
            size_t bodyStart = state.request.find("\r\n\r\n");
            if (bodyStart != std::string::npos) {
                bodyStart += 4;
                if (bodyStart + contentLength <= state.request.length()) {
                    jsonBody = state.request.substr(bodyStart, contentLength);
                }
            }
        }
    }
    
    // Parse JSON config
    Config newConfig = config_; // Start with current config
    
    // Simple JSON parsing for key fields
    auto extractValue = [&](const std::string& key) -> std::string {
        std::string searchKey = "\"" + key + "\":\"";
        size_t pos = jsonBody.find(searchKey);
        if (pos != std::string::npos) {
            size_t start = pos + searchKey.length();
            size_t end = jsonBody.find("\"", start);
            if (end != std::string::npos) {
                return jsonBody.substr(start, end - start);
            }
        }
        return "";
    };
    
    auto extractNumber = [&](const std::string& key) -> int {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = jsonBody.find(searchKey);
        if (pos != std::string::npos) {
            size_t start = pos + searchKey.length();
            size_t end = jsonBody.find_first_of(",}", start);
            if (end != std::string::npos) {
                return std::stoi(jsonBody.substr(start, end - start));
            }
        }
        return 0;
    };
    
    auto extractBool = [&](const std::string& key) -> bool {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = jsonBody.find(searchKey);
        if (pos != std::string::npos) {
            size_t start = pos + searchKey.length();
            size_t end = jsonBody.find_first_of(",}", start);
            if (end != std::string::npos) {
                std::string value = jsonBody.substr(start, end - start);
                return value == "true";
            }
        }
        return false;
    };
    
    // Update config with new values
    newConfig.documentRoot = extractValue("wwwRoot");
    // Ensure www_root ends with a slash
    if (!newConfig.documentRoot.empty() && newConfig.documentRoot.back() != '/') {
        newConfig.documentRoot += '/';
    }
    newConfig.indexFile = extractValue("indexFile");
    newConfig.enableLogging = extractBool("enableLogging");
    newConfig.enableDirectoryListing = extractBool("directoryListing");
    newConfig.logRotation.maxSizeBytes = extractNumber("logMaxSize"); // Value is already in bytes
    newConfig.logRotation.maxFiles = extractNumber("logMaxFiles");
    
    // Debug: log extracted values
    logFile_.writeString("DEBUG: Extracted logMaxSizeBytes: " + std::to_string(newConfig.logRotation.maxSizeBytes) + "\n");
    logFile_.writeString("DEBUG: Extracted logMaxFiles: " + std::to_string(newConfig.logRotation.maxFiles) + "\n");
    logFile_.flush();
    
    // Update bind address and port if provided
    std::string newBindAddress = extractValue("bindAddress");
    int newHttpPort = extractNumber("httpPort");
    if (!newBindAddress.empty()) {
        bind_.address = newBindAddress;
    }
    if (newHttpPort > 0) {
        bind_.port = Port{static_cast<uint16_t>(newHttpPort)};
    }
    
    // Write config to server.conf file
    std::string configContent = "# Server configuration\n";
    configContent += "bind_address=" + bind_.address + "\n";
    configContent += "http_port=" + std::to_string(newHttpPort > 0 ? newHttpPort : bind_.port.value()) + "\n";
    configContent += "www_root=" + newConfig.documentRoot + "\n";
    configContent += "index_file=" + newConfig.indexFile + "\n";
    configContent += "enable_logging=" + std::string(newConfig.enableLogging ? "true" : "false") + "\n";
    configContent += "directory_listing=" + std::string(newConfig.enableDirectoryListing ? "true" : "false") + "\n";
    configContent += "log_max_size=" + std::to_string(newConfig.logRotation.maxSizeBytes / (1024 * 1024)) + "\n";
    configContent += "log_max_files=" + std::to_string(newConfig.logRotation.maxFiles) + "\n";
    
    // HTTPS settings
    bool enableHttps = extractBool("enableHttps");
    int httpsPort = extractNumber("httpsPort");
    std::string cert = extractValue("cert");
    std::string key = extractValue("key");
    
    if (enableHttps || httpsPort > 0 || !cert.empty() || !key.empty()) {
        configContent += "enable_https=" + std::string(enableHttps ? "true" : "false") + "\n";
        if (httpsPort > 0) {
            configContent += "https_port=" + std::to_string(httpsPort) + "\n";
        }
        if (!cert.empty()) {
            configContent += "cert=" + cert + "\n";
        }
        if (!key.empty()) {
            configContent += "key=" + key + "\n";
        }
    }
    
    File configFile("server.conf", "w");
    if (!configFile.isOpen()) {
        std::string json = R"({"success": false, "message": "Failed to write config file: server.conf"})";
        std::string response;
        response.reserve(256 + json.size());
        response.append("HTTP/1.1 500 Internal Server Error\r\n");
        response.append("Content-Type: application/json\r\nContent-Length: ");
        response += std::to_string(json.size());
        response.append("\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
        response.append(json);
        state.dataBuf = std::move(response);
        state.dataView = state.dataBuf;
        return;
    }
    
    configFile.writeString(configContent);
    
    // Update the in-memory config
    config_ = newConfig;
    
    std::string json = R"({"success": true, "message": "Configuration saved successfully to server.conf. Restart the server to apply changes."})";
    std::string response;
    response.reserve(256 + json.size());
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: application/json\r\nContent-Length: ");
    response += std::to_string(json.size());
    response.append("\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
    response.append(json);
    
    state.dataBuf = std::move(response);
    state.dataView = state.dataBuf;
}

void HttpFileServer::logRequest(const HttpRequest& request, const HttpClientState& state) {
    if (!config_.enableLogging || !logFile_.isOpen()) return;

    // Check if rotation is needed
    if (logRotation_.shouldRotate()) {
        logFile_.close();
        if (logRotation_.rotate()) {
            logFile_.open(logRotation_.getLogPath().c_str(), "a");
        } else {
            // If rotation fails, try to reopen the original file
            logFile_.open(logRotation_.getLogPath().c_str(), "a");
        }
    }

    // Write basic log entry (can be overridden in derived classes)
    logFile_.printf("%s - - [%s] \"%s %s %s\" 200 -\n",
        state.peerAddress.c_str(),
        "timestamp",
        request.method.c_str(),
        request.path.c_str(),
        request.version.c_str());
    logFile_.flush();
}

} // namespace aiSocks
