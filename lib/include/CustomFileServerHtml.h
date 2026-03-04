// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com


#pragma once

#include <string>

namespace CustomFileServerHtml {

    // Error page HTML
    inline const char* ERROR_PAGE_HTML = 
        "<!DOCTYPE html>\n"
        "<html><head>\n"
        "<title>404 - File Not Found</title>\n"
        "<style>\n"
        "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }\n"
        ".container { max-width: 800px; margin: 50px auto; background: white; padding: 40px; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }\n"
        "h1 { color: #e74c3c; margin-bottom: 20px; }\n"
        ".error-code { font-size: 48px; font-weight: bold; color: #e74c3c; margin-bottom: 10px; }\n"
        ".error-message { font-size: 18px; color: #555; margin-bottom: 30px; }\n"
        ".back-link { display: inline-block; background: #3498db; color: white; padding: 12px 24px; text-decoration: none; border-radius: 5px; transition: background 0.3s; }\n"
        ".back-link:hover { background: #2980b9; }\n"
        "</style></head><body>\n"
        "<div class=\"container\">\n"
        "<div class=\"error-code\">404</div>\n"
        "<h1>File Not Found</h1>\n"
        "<div class=\"error-message\">The file you requested could not be found on this server.</div>\n"
        "<a href=\"/\" class=\"back-link\">← Back to Home</a>\n"
        "</div>\n"
        "</body></html>\n";

    // Directory listing footer
    inline const char* DIRECTORY_LISTING_FOOTER = 
        "<div class=\"footer\">\n"
        "<p>Custom File Server | Built with aiSocks HttpFileServer</p>\n"
        "</div>\n"
        "</div>\n"
        "</body></html>\n";

    // Demo page start
    inline const char* DEMO_PAGE_START = 
        "<!DOCTYPE html>\n"
        "<html><head>\n"
        "<title>JavaScript Demo - test.js</title>\n"
        "<style>\n"
        "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); }\n";

} // namespace CustomFileServerHtml
