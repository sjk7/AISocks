#ifndef AISOCKS_MIME_TYPES_H
#define AISOCKS_MIME_TYPES_H

#include <map>
#include <string>

namespace aiSocks {
namespace MimeTypes {

// Returns the MIME type for the given file path based on its extension.
// Falls back to "application/octet-stream" for unknown extensions.
inline std::string fromPath(const std::string& filePath) {
    static const std::map<std::string, std::string> table = {
        {".html",  "text/html"},
        {".htm",   "text/html"},
        {".css",   "text/css"},
        {".js",    "application/javascript"},
        {".json",  "application/json"},
        {".xml",   "application/xml"},
        {".txt",   "text/plain"},
        {".md",    "text/markdown"},
        {".png",   "image/png"},
        {".jpg",   "image/jpeg"},
        {".jpeg",  "image/jpeg"},
        {".gif",   "image/gif"},
        {".svg",   "image/svg+xml"},
        {".ico",   "image/x-icon"},
        {".pdf",   "application/pdf"},
        {".zip",   "application/zip"},
        {".gz",    "application/gzip"},
        {".mp3",   "audio/mpeg"},
        {".mp4",   "video/mp4"},
        {".webm",  "video/webm"},
        {".woff",  "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf",   "font/ttf"},
        {".eot",   "application/vnd.ms-fontobject"},
    };

    // Extract extension — last '.' to end, e.g. "/foo/bar.HTML" -> ".HTML"
    const size_t dot = filePath.rfind('.');
    if (dot == std::string::npos || dot + 1 == filePath.size())
        return "application/octet-stream";

    const std::string ext = filePath.substr(dot);

    // Case-insensitive lookup: convert extension to lowercase.
    std::string extLower;
    extLower.reserve(ext.size());
    for (char c : ext)
        extLower += static_cast<char>(
            (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);

    auto it = table.find(extLower);
    return it != table.end() ? it->second : "application/octet-stream";
}

} // namespace MimeTypes
} // namespace aiSocks

#endif // AISOCKS_MIME_TYPES_H
