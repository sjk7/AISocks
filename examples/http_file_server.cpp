// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Example: Simple HTTP file server using HttpFileServer
// Serves files from the current directory

#include "HttpFileServer.h"
#include <iostream>

using namespace aiSocks;

int main() {
    std::cout << "=== HTTP File Server Example ===\n";
    
    // Configure the file server
    HttpFileServer::Config config;
    config.documentRoot = "./www";          // Serve files from ./www directory
    config.indexFile = "index.html";        // Default file for directories
    config.enableDirectoryListing = true;   // Show directory contents
    config.enableETag = true;               // Enable ETag headers
    config.enableLastModified = true;       // Enable Last-Modified headers
    config.maxFileSize = 50 * 1024 * 1024;  // 50MB max file size
    
    // Add custom headers
    config.customHeaders["Server"] = "aiSocks-FileServer/1.0";
    config.customHeaders["X-Content-Type-Options"] = "nosniff";
    
    try {
        // Create and start the server
        HttpFileServer server(ServerBind{"0.0.0.0", Port{8080}}, config);
        
        std::cout << "Server starting on http://localhost:8080/\n";
        std::cout << "Serving files from: " << config.documentRoot << "\n";
        std::cout << "Directory listing: " << (config.enableDirectoryListing ? "enabled" : "disabled") << "\n";
        std::cout << "Press Ctrl+C to stop the server\n\n";
        
        // Run the server (blocking call)
        server.run(ClientLimit::Unlimited, Milliseconds{0});
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
