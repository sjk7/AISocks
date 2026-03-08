// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include "HtmlEscape.h"
#include "PathHelper.h"
#include "UrlCodec.h"
#include <cstdio>
#include <string>
#include <vector>

namespace aiSocks {

// ---------------------------------------------------------------------------
// HtmlPageGenerator
//
// Generates complete HTML documents for error responses and directory listings.
// Pure presentation logic — no server state, no sockets, no file I/O.
//
// hideServerVersion controls whether the "aiSocks HttpFileServer" footer
// appears at the bottom of generated pages.
// ---------------------------------------------------------------------------
class HtmlPageGenerator {
    public:
    explicit HtmlPageGenerator(bool hideServerVersion = true)
        : hideServerVersion_(hideServerVersion) {}

    // Generates a simple error page.
    // `message` is HTML-escaped internally — safe to pass user-derived strings.
    std::string errorPage(
        int code, const std::string& status, const std::string& message) const {
        char cbuf[8];
        std::snprintf(cbuf, sizeof(cbuf), "%d", code);

        std::string html;
        html.reserve(512);
        html += "<!DOCTYPE html>\n<html><head><title>";
        html += cbuf;
        html += " ";
        html += HtmlEscape::encode(status);
        html += "</title></head>\n<body><h1>";
        html += cbuf;
        html += " ";
        html += HtmlEscape::encode(status);
        html += "</h1>\n<p>";
        html += HtmlEscape::encode(message);
        html += "</p>\n";
        if (!hideServerVersion_)
            html += "<hr><address>aiSocks HttpFileServer</address>\n";
        html += "</body></html>";
        return html;
    }

    // Generates a directory listing page.
    std::string directoryListing(const std::string& dirPath) const {
        std::vector<PathHelper::DirEntry> entries
            = PathHelper::listDirectory(dirPath);

        std::string html;
        html.reserve(2048);
        html += "<!DOCTYPE html>\n"
                "<html><head><title>Directory listing</title></head>\n"
                "<body><h1>Directory listing</h1>\n<ul>\n";

        if (entries.empty()) {
            html += "<li>Error reading directory</li>\n";
        } else {
            for (const auto& entry : entries) {
                const std::string& name = entry.name;
                if (name.empty() || name[0] == '.') continue;
                bool isDir = entry.isDirectory;
                html += "<li><a href=\"";
                html += urlEncode(name);
                if (isDir) html += "/";
                html += "\">";
                html += HtmlEscape::encode(name);
                if (isDir) html += "/";
                html += "</a></li>\n";
            }
        }

        html += "</ul>\n";
        if (!hideServerVersion_)
            html += "<hr><address>aiSocks HttpFileServer</address>\n";
        html += "</body></html>";
        return html;
    }

    private:
    bool hideServerVersion_;
};

} // namespace aiSocks
