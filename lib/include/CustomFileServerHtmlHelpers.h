// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com


#pragma once

#include <string>

namespace CustomFileServerHtmlHelpers {

    /// Generate directory listing footer HTML
    inline std::string generateDirectoryListingFooter() {
        return 
            "<div class=\"footer\">\n"
            "<p>Custom File Server | Built with aiSocks HttpFileServer</p>\n"
            "</div>\n"
            "</div>\n"
            "</body></html>\n";
    }

    /// Generate demo page start HTML
    inline std::string generateDemoPageStart() {
        return 
            "<!DOCTYPE html>\n"
            "<html><head>\n"
            "<title>JavaScript Demo - test.js</title>\n"
            "<style>\n"
            "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); }\n";
    }

} // namespace CustomFileServerHtmlHelpers
