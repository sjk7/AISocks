// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Example: Custom HTTP file server with authentication and logging
// Demonstrates how to derive from HttpFileServer and override virtual functions

#include "HttpFileServer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>

using namespace aiSocks;

/// Custom file server with authentication and access logging
class CustomFileServer : public HttpFileServer {
public:
    explicit CustomFileServer(const ServerBind& bind, const Config& config = {})
        : HttpFileServer(bind, config) {
        
        // Open log file
        logFile_.open("access.log", std::ios::app);
        
        // Note: Authentication header will be added by the base class
    }

    ~CustomFileServer() {
        if (logFile_.is_open()) {
            logFile_.close();
        }
    }

protected:
    /// Override to add access logging
    void buildResponse(HttpClientState& state) override {
        // Parse request first
        auto request = HttpRequest::parse(state.request);
        
        // Log the request
        if (request.valid) {
            logRequest(request, state);
        }
        
        // Check authentication
        if (!isAuthenticated(request)) {
            sendAuthRequired(state);
            return;
        }
        
        // Call base implementation
        HttpFileServer::buildResponse(state);
    }

    /// Override to add custom MIME types
    std::string getMimeType(const std::string& filePath) const override {
        std::string ext = getFileExtension(filePath);
        
        // Add custom MIME types
        if (ext == ".wasm") return "application/wasm";
        if (ext == ".ts") return "application/typescript";
        if (ext == ".jsx") return "text/jsx";
        if (ext == ".tsx") return "text/tsx";
        
        // Fall back to base implementation
        return HttpFileServer::getMimeType(filePath);
    }

    /// Override to add access control
    bool isAccessAllowed(const std::string& filePath, const FileInfo& fileInfo) const override {
        // Call base implementation first
        if (!HttpFileServer::isAccessAllowed(filePath, fileInfo)) {
            return false;
        }
        
        // Additional custom checks
        std::string ext = getFileExtension(filePath);
        
        // Deny access to sensitive files
        if (ext == ".conf" || ext == ".log" || ext == ".tmp") {
            return false;
        }
        
        // Allow access to everything else
        return true;
    }

    /// Override to customize error pages
    std::string generateErrorHtml(int code, const std::string& status, const std::string& message) const override {
        std::ostringstream html;
        html << "<!DOCTYPE html>\n";
        html << "<html><head>\n";
        html << "<title>" << code << " " << status << "</title>\n";
        html << "<style>\n";
        html << "body { font-family: Arial, sans-serif; margin: 40px; background: #f5f5f5; }\n";
        html << ".error-container { background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }\n";
        html << "h1 { color: #e74c3c; }\n";
        html << ".back-link { color: #3498db; text-decoration: none; }\n";
        html << "</style>\n";
        html << "</head><body>\n";
        html << "<div class=\"error-container\">\n";
        html << "<h1>" << code << " " << status << "</h1>\n";
        html << "<p>" << message << "</p>\n";
        html << "<p><a href=\"/\" class=\"back-link\">← Back to Home</a></p>\n";
        html << "<hr><p><small>Custom File Server | " << getCurrentTime() << "</small></p>\n";
        html << "</div></body></html>";
        return html.str();
    }

    /// Override to customize directory listing
    std::string generateDirectoryListing(const std::string& dirPath) const override {
        std::ostringstream html;
        html << "<!DOCTYPE html>\n";
        html << "<html><head>\n";
        html << "<title>Directory: " << dirPath << "</title>\n";
        html << "<style>\n";
        html << "body { font-family: Arial, sans-serif; margin: 20px; }\n";
        html << "table { border-collapse: collapse; width: 100%; }\n";
        html << "th, td { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }\n";
        html << "th { background-color: #f2f2f2; }\n";
        html << "a { text-decoration: none; color: #3498db; }\n";
        html << "a:hover { text-decoration: underline; }\n";
        html << "</style>\n";
        html << "</head><body>\n";
        html << "<h1>📁 Directory listing: " << dirPath << "</h1>\n";
        html << "<table>\n";
        html << "<tr><th>Name</th><th>Type</th><th>Size</th><th>Modified</th></tr>\n";
        
        // Add parent directory link
        if (dirPath != getConfig().documentRoot) {
            html << "<tr><td><a href=\"../\">📁 ../</a></td><td>Directory</td><td>-</td><td>-</td></tr>\n";
        }
        
        try {
            std::vector<std::pair<std::string, bool>> entries; // name, isDirectory
            
            for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                std::string name = entry.path().filename().string();
                if (name.empty() || name[0] == '.') continue; // Skip hidden files
                
                entries.emplace_back(name, entry.is_directory());
            }
            
            // Sort: directories first, then files, both alphabetically
            std::sort(entries.begin(), entries.end(), 
                [](const auto& a, const auto& b) {
                    if (a.second != b.second) return a.second; // directories first
                    return a.first < b.first; // alphabetical
                });
            
            for (const auto& [name, isDir] : entries) {
                std::string fullPath = dirPath + "/" + name;
                std::string icon = isDir ? "📁" : "📄";
                std::string type = isDir ? "Directory" : getMimeType(fullPath);
                std::string size = "-";
                std::string modified = "-";
                
                if (!isDir) {
                    try {
                        auto fileSize = std::filesystem::file_size(fullPath);
                        if (fileSize < 1024) {
                            size = std::to_string(fileSize) + " B";
                        } else if (fileSize < 1024 * 1024) {
                            size = std::to_string(fileSize / 1024) + " KB";
                        } else {
                            size = std::to_string(fileSize / (1024 * 1024)) + " MB";
                        }
                        
                        auto modTime = std::filesystem::last_write_time(fullPath);
                        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                            modTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
                        std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
                        std::ostringstream oss;
                        oss << std::put_time(std::localtime(&cftime), "%Y-%m-%d %H:%M");
                        modified = oss.str();
                    } catch (...) {
                        // Ignore errors for file stats
                    }
                }
                
                html << "<tr><td><a href=\"" << name << (isDir ? "/" : "") << "\">";
                html << icon << " " << name << (isDir ? "/" : "") << "</a></td>";
                html << "<td>" << type << "</td>";
                html << "<td>" << size << "</td>";
                html << "<td>" << modified << "</td></tr>\n";
            }
        } catch (const std::exception& e) {
            html << "<tr><td colspan=\"4\">Error reading directory: " << e.what() << "</td></tr>\n";
        }
        
        html << "</table>\n";
        html << "<hr><p><small>Custom File Server | " << getCurrentTime() << "</small></p>\n";
        html << "</body></html>";
        return html.str();
    }

private:
    std::ofstream logFile_;
    
    void logRequest(const HttpRequest& request, const HttpClientState& state) {
        if (!logFile_.is_open()) return;
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        logFile_ << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        logFile_ << " " << request.method << " " << request.path;
        logFile_ << " from client";
        logFile_ << std::endl;
        logFile_.flush();
    }
    
    bool isAuthenticated(const HttpRequest& request) const {
        // Simple basic authentication (username: admin, password: secret)
        auto authIt = request.headers.find("authorization");
        if (authIt == request.headers.end()) {
            return false;
        }
        
        const std::string& auth = authIt->second;
        if (auth.substr(0, 6) != "Basic ") {
            return false;
        }
        
        // In a real implementation, you'd decode the Base64 credentials
        // For this example, we'll just check for a specific token
        return auth.substr(6) == "YWRtaW46c2VjcmV0"; // "admin:secret" in Base64
    }
    
    void sendAuthRequired(HttpClientState& state) {
        std::string htmlBody = generateErrorHtml(401, "Unauthorized", 
            "This server requires authentication. Please provide valid credentials.");
        
        std::ostringstream response;
        response << "HTTP/1.1 401 Unauthorized\r\n";
        response << "Content-Type: text/html\r\n";
        response << "Content-Length: " << htmlBody.size() << "\r\n";
        response << "WWW-Authenticate: Basic realm=\"Secure Area\"\r\n";
        response << "\r\n" << htmlBody;
        
        state.responseBuf = response.str();
        state.responseView = state.responseBuf;
    }
    
    std::string getCurrentTime() const {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
    
    std::string getFileExtension(const std::string& filePath) const {
        size_t dotPos = filePath.find_last_of('.');
        if (dotPos != std::string::npos && dotPos < filePath.length() - 1) {
            return filePath.substr(dotPos);
        }
        return "";
    }
};

int main() {
    std::cout << "=== Custom HTTP File Server Example ===\n";
    
    // Configure the file server
    HttpFileServer::Config config;
    config.documentRoot = "./www";          // Serve files from ./www directory
    config.indexFile = "index.html";        // Default file for directories
    config.enableDirectoryListing = true;   // Show directory contents
    config.enableETag = true;               // Enable ETag headers
    config.enableLastModified = true;       // Enable Last-Modified headers
    config.maxFileSize = 50 * 1024 * 1024;  // 50MB max file size
    
    // Add custom headers
    config.customHeaders["Server"] = "Custom-FileServer/1.0";
    config.customHeaders["X-Content-Type-Options"] = "nosniff";
    config.customHeaders["X-Frame-Options"] = "DENY";
    
    try {
        // Create and start the custom server
        CustomFileServer server(ServerBind{"0.0.0.0", Port{8080}}, config);
        
        std::cout << "Custom HTTP File Server starting on http://localhost:8080/\n";
        std::cout << "Serving files from: " << config.documentRoot << "\n";
        std::cout << "Features:\n";
        std::cout << "  - Basic authentication (admin:secret)\n";
        std::cout << "  - Access logging to access.log\n";
        std::cout << "  - Enhanced directory listings\n";
        std::cout << "  - Custom error pages\n";
        std::cout << "  - Access control (denies .conf, .log, .tmp files)\n";
        std::cout << "\nPress Ctrl+C to stop the server\n";
        
        // Run the server (blocking call)
        server.run(ClientLimit::Unlimited, Milliseconds{0});
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
