// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Example: Simple HTTP file server using HttpFileServer
// Serves files from the current directory

#include "HttpFileServer.h"
#include <cstdio>

using namespace aiSocks;

int main() {
    printf("=== HTTP File Server Example ===\n");

    // Configure the file server
    HttpFileServer::Config config;
    config.documentRoot = "./www"; // Serve files from ./www directory
    config.indexFile = "index.html"; // Default file for directories
    config.enableDirectoryListing = true; // Show directory contents
    config.enableETag = true; // Enable ETag headers
    config.enableLastModified = true; // Enable Last-Modified headers
    config.maxFileSize = 50 * 1024 * 1024; // 50MB max file size

    // Add custom headers
    config.customHeaders["Server"] = "aiSocks-FileServer/1.0";
    config.customHeaders["X-Content-Type-Options"] = "nosniff";

    // Create the server
    HttpFileServer server(ServerBind{"0.0.0.0", Port{8080}}, config);

    // Check if server creation succeeded (bind/listen)
    if (!server.isValid()) {
        fprintf(stderr, "Error: Failed to start server on port 8080\n");
        fprintf(stderr, "This usually means:\n");
        fprintf(stderr, "  - Another process is already using port 8080\n");
        fprintf(stderr, "  - Insufficient permissions to bind to the port\n");
        return 1;
    }

    // Print build info immediately after title
    server.printBuildInfo();
    printf("Server starting on http://localhost:8080/\n");
    printf("Serving files from: %s\n", config.documentRoot.c_str());
    printf("Directory listing: enabled\n");
    printf("Press Ctrl+C to stop the server\n\n");

    // Run the server (blocking call)
    server.run(ClientLimit::Unlimited, Milliseconds{0});

    return 0;
}
