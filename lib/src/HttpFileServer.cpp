// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "HttpFileServer.h"

#include "FileIO.h"
#include "PathHelper.h"
#include "UrlCodec.h"

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

    if (!PathHelper::isPathWithin(filePath, config_.documentRoot)) {
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

    path = urlDecodePath(path);

    if (path == "/") {
        path = "/" + config_.indexFile;
    }

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

std::string HttpFileServer::getMimeType(const std::string& filePath) const {
    std::string ext = getFileExtension(filePath);

    static const std::map<std::string, std::string> mimeTypes = {
        {".html", "text/html"}, {".htm", "text/html"}, {".css", "text/css"},
        {".js", "application/javascript"}, {".json", "application/json"},
        {".xml", "application/xml"}, {".txt", "text/plain"},
        {".md", "text/markdown"}, {".png", "image/png"}, {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"}, {".gif", "image/gif"},
        {".svg", "image/svg+xml"}, {".ico", "image/x-icon"},
        {".pdf", "application/pdf"}, {".zip", "application/zip"},
        {".gz", "application/gzip"}, {".mp3", "audio/mpeg"},
        {".mp4", "video/mp4"}, {".webm", "video/webm"}, {".woff", "font/woff"},
        {".woff2", "font/woff2"}, {".ttf", "font/ttf"},
        {".eot", "application/vnd.ms-fontobject"}};

    auto it = mimeTypes.find(ext);
    if (it != mimeTypes.end()) {
        return it->second;
    }

    return "application/octet-stream";
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
            lastModified.append(formatHttpDate(fileInfo.lastModified));
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
    response.append(getMimeType(filePath));
    if (getMimeType(filePath).find("text/") == 0
        || getMimeType(filePath) == "application/javascript") {
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

    addSecurityHeaders(response);

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

    addSecurityHeaders(response);

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
        response.append(formatHttpDate(fileInfo.lastModified));
        response.append("\r\n");
    }

    if (config_.enableETag && !fileInfo.etag.empty()) {
        response.append("ETag: ");
        response.append(fileInfo.etag);
        response.append("\r\n");
    }

    addSecurityHeaders(response);

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
    response.append(getMimeType(filePath));
    if (getMimeType(filePath).find("text/") == 0
        || getMimeType(filePath) == "application/javascript") {
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

    addSecurityHeaders(response);

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

    addSecurityHeaders(response);

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
    StringBuilder html(512);
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

std::string HttpFileServer::generateDirectoryListing(
    const std::string& dirPath) const {
    StringBuilder html(2048);
    html.append("<!DOCTYPE html>\n");
    html.append("<html><head><title>Directory listing</title></head>\n");
    html.append("<body><h1>Directory listing</h1>\n");
    html.append("<ul>\n");

    std::vector<PathHelper::DirEntry> entries
        = PathHelper::listDirectory(dirPath);
    if (entries.empty()) {
        html.append("<li>Error reading directory</li>\n");
    } else {
        for (const auto& entry : entries) {
            const std::string& name = entry.name;
            if (name.empty() || name[0] == '.') continue;

            bool isDir = entry.isDirectory;

            html.append("<li><a href=\"");
            html.append(urlEncode(name));
            if (isDir) html.append("/");
            html.append("\">");
            html.append(htmlEscape(name));
            if (isDir) html.append("/");
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

const HttpFileServer::Config& HttpFileServer::getConfig() const {
    return config_;
}

FileCache& HttpFileServer::getFileCache() {
    return fileCache_;
}

void HttpFileServer::addSecurityHeaders(StringBuilder& response) const {
    if (!config_.enableSecurityHeaders) return;

    response.append("X-Content-Type-Options: nosniff\r\n");
    response.append("X-Frame-Options: DENY\r\n");
    response.append(
        "Content-Security-Policy: default-src 'self'; style-src 'self' "
        "'unsafe-inline'; script-src 'self' 'unsafe-inline'\r\n");
    response.append("Referrer-Policy: no-referrer\r\n");
}

std::string HttpFileServer::htmlEscape(const std::string& str) {
    std::string escaped;
    escaped.reserve(str.size());
    for (char c : str) {
        switch (c) {
            case '&': escaped.append("&amp;"); break;
            case '<': escaped.append("&lt;"); break;
            case '>': escaped.append("&gt;"); break;
            case '"': escaped.append("&quot;"); break;
            case '\'': escaped.append("&#39;"); break;
            default: escaped.push_back(c); break;
        }
    }
    return escaped;
}

std::string HttpFileServer::urlDecodePath(const std::string& src) {
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
        out += static_cast<char>(c);
    }
    return out;
}

std::string HttpFileServer::getFileExtension(
    const std::string& filePath) const {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos != std::string::npos && dotPos < filePath.length() - 1) {
        return filePath.substr(dotPos);
    }
    return "";
}

std::string HttpFileServer::formatHttpDate(time_t fileTime) const {
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

} // namespace aiSocks
