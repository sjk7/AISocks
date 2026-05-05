// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests for HttpFileServer config editor API handlers

#include "HttpFileServer.h"
#include "test_helpers.h"

#include <string>
#include <fstream>

using namespace aiSocks;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

class TestFileServer : public HttpFileServer {
    public:
    TestFileServer(const ServerBind& bind, const Config& config, Result<TcpSocket>* result = nullptr)
        : HttpFileServer(bind, config, result) {}
    
    // Expose protected handlers for testing
    void testHandleGetAvailableIPs(HttpClientState& state) {
        handleGetAvailableIPs(state);
    }
    
    void testHandleGetCurrentConfig(HttpClientState& state) {
        handleGetCurrentConfig(state);
    }
    
    void testHandleSaveConfig(HttpClientState& state, const HttpRequest& request) {
        handleSaveConfig(state, request);
    }
    
    bool testIsLocalClient(const std::string& address) {
        return isLocalClient(address);
    }
    
    void testBuildResponse(HttpClientState& state) {
        buildResponse(state);
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

int main() {
    printf("Testing HttpFileServer config editor API handlers\n");
    
    int passed = 0;
    int failed = 0;
    
    // Test 1: handleGetAvailableIPs
    {
        printf("Test 1: handleGetAvailableIPs... ");
        HttpFileServer::Config config;
        config.documentRoot = "./www";
        TestFileServer server(ServerBind{"127.0.0.1", Port{18080}}, config);
        
        HttpClientState state;
        state.peerAddress = "127.0.0.1";
        
        server.testHandleGetAvailableIPs(state);
        
        std::string response(state.dataView.data(), state.dataView.size());
        if (response.find("\"ips\":") != std::string::npos && 
            response.find("HTTP/1.1 200 OK") != std::string::npos) {
            printf("PASSED\n");
            passed++;
        } else {
            printf("FAILED (invalid response)\n");
            failed++;
        }
    }
    
    // Test 2: handleGetCurrentConfig includes bindAddress and httpPort
    {
        printf("Test 2: handleGetCurrentConfig includes bindAddress and httpPort... ");
        HttpFileServer::Config config;
        config.documentRoot = "./www";
        config.enableLogging = true;
        TestFileServer server(ServerBind{"127.0.0.1", Port{18081}}, config);
        
        HttpClientState state;
        state.peerAddress = "127.0.0.1";
        
        server.testHandleGetCurrentConfig(state);
        
        std::string response(state.dataView.data(), state.dataView.size());
        if (response.find("\"bindAddress\": \"127.0.0.1\"") != std::string::npos && 
            response.find("\"httpPort\": 18081") != std::string::npos &&
            response.find("HTTP/1.1 200 OK") != std::string::npos) {
            printf("PASSED\n");
            passed++;
        } else {
            printf("FAILED (bindAddress or httpPort missing)\n");
            failed++;
        }
    }
    
    // Test 3: handleGetCurrentConfig includes wwwRoot and enableLogging
    {
        printf("Test 3: handleGetCurrentConfig includes wwwRoot and enableLogging... ");
        HttpFileServer::Config config;
        config.documentRoot = "./www";
        config.enableLogging = true;
        TestFileServer server(ServerBind{"127.0.0.1", Port{18082}}, config);
        
        HttpClientState state;
        state.peerAddress = "127.0.0.1";
        
        server.testHandleGetCurrentConfig(state);
        
        std::string response(state.dataView.data(), state.dataView.size());
        if (response.find("\"wwwRoot\"") != std::string::npos && 
            response.find("\"enableLogging\": true") != std::string::npos &&
            response.find("HTTP/1.1 200 OK") != std::string::npos) {
            printf("PASSED\n");
            passed++;
        } else {
            printf("FAILED (invalid response)\n");
            failed++;
        }
    }
    
    // Test 4: handleGetAvailableIPs returns actual IPs from library
    {
        printf("Test 4: handleGetAvailableIPs returns actual IPs from library... ");
        HttpFileServer::Config config;
        config.documentRoot = "./www";
        TestFileServer server(ServerBind{"127.0.0.1", Port{18083}}, config);
        
        HttpClientState state;
        state.peerAddress = "127.0.0.1";
        
        server.testHandleGetAvailableIPs(state);
        
        std::string response(state.dataView.data(), state.dataView.size());
        // Check that we get at least localhost IP
        if (response.find("\"ips\":") != std::string::npos && 
            (response.find("127.0.0.1") != std::string::npos || response.find("::1") != std::string::npos) &&
            response.find("HTTP/1.1 200 OK") != std::string::npos) {
            printf("PASSED\n");
            passed++;
        } else {
            printf("FAILED (no IPs returned)\n");
            failed++;
        }
    }
    
    // Test 5: handleSaveConfig
    {
        printf("Test 5: handleSaveConfig... ");
        HttpFileServer::Config config;
        config.documentRoot = "./www";
        TestFileServer server(ServerBind{"127.0.0.1", Port{18084}}, config);
        
        HttpClientState state;
        state.peerAddress = "127.0.0.1";
        HttpRequest request;
        request.valid = true;
        request.method = "POST";
        request.path = "/api/config/save";
        
        server.testHandleSaveConfig(state, request);
        
        std::string response(state.dataView.data(), state.dataView.size());
        if (response.find("HTTP/1.1 200 OK") != std::string::npos) {
            printf("PASSED\n");
            passed++;
        } else {
            printf("FAILED (invalid response)\n");
            failed++;
        }
    }
    
    // Test 6: Local-only access check (via isLocalClient helper)
    {
        printf("Test 6: Local-only access check... ");
        HttpFileServer::Config config;
        config.documentRoot = "./www";
        TestFileServer server(ServerBind{"127.0.0.1", Port{18085}}, config);
        
        bool isLocal = server.testIsLocalClient("127.0.0.1");
        bool isNonLocal = server.testIsLocalClient("192.168.1.1");
        
        if (isLocal && !isNonLocal) {
            printf("PASSED\n");
            passed++;
        } else {
            printf("FAILED (isLocalClient check incorrect)\n");
            failed++;
        }
    }
    
    // Test 7: handleSaveConfig with log_max_size
    {
        printf("Test 7: handleSaveConfig saves log_max_size... ");
        HttpFileServer::Config config;
        config.documentRoot = "./www";
        TestFileServer server(ServerBind{"127.0.0.1", Port{18086}}, config);
        
        HttpClientState state;
        state.peerAddress = "127.0.0.1";
        HttpRequest request;
        request.valid = true;
        request.method = "POST";
        request.path = "/api/config/save";
        // JSON with log_max_size = 100MB (in bytes)
        state.request = R"({
            "logMaxSizeBytes": 104857600,
            "logMaxFiles": 5
        })";
        
        server.testHandleSaveConfig(state, request);
        
        // Read server.conf to check if log_max_size was saved
        std::ifstream confFile("server.conf");
        bool foundLogMaxSize = false;
        if (confFile.is_open()) {
            std::string line;
            while (std::getline(confFile, line)) {
                if (line.find("log_max_size=100") != std::string::npos) {
                    foundLogMaxSize = true;
                    break;
                }
            }
            confFile.close();
        }
        
        std::string response(state.dataView.data(), state.dataView.size());
        if (response.find("HTTP/1.1 200 OK") != std::string::npos && foundLogMaxSize) {
            printf("PASSED\n");
            passed++;
        } else {
            printf("FAILED (log_max_size not saved correctly)\n");
            failed++;
        }
    }

    // Test 7: Different bind addresses (0.0.0.0)
    {
        printf("Test 7: Config with 0.0.0.0 bind address... ");
        HttpFileServer::Config config;
        config.documentRoot = "./www";
        TestFileServer server(ServerBind{"0.0.0.0", Port{18086}}, config);
        
        HttpClientState state;
        state.peerAddress = "127.0.0.1";
        
        server.testHandleGetCurrentConfig(state);
        
        std::string response(state.dataView.data(), state.dataView.size());
        if (response.find("\"bindAddress\": \"0.0.0.0\"") != std::string::npos && 
            response.find("HTTP/1.1 200 OK") != std::string::npos) {
            printf("PASSED\n");
            passed++;
        } else {
            printf("FAILED (0.0.0.0 not in config)\n");
            failed++;
        }
    }
    
    // Test 8: Save config with minimal/default values (no changes)
    {
        printf("Test 8: Save config with default values... ");
        HttpFileServer::Config config;
        config.documentRoot = "./www";
        TestFileServer server(ServerBind{"127.0.0.1", Port{18087}}, config);
        
        HttpClientState state;
        state.peerAddress = "127.0.0.1";
        HttpRequest request;
        request.valid = true;
        request.method = "POST";
        request.path = "/api/config/save";
        
        server.testHandleSaveConfig(state, request);
        
        std::string response(state.dataView.data(), state.dataView.size());
        if (response.find("HTTP/1.1 200 OK") != std::string::npos) {
            printf("PASSED\n");
            passed++;
        } else {
            printf("FAILED (save with defaults failed)\n");
            failed++;
        }
    }
    
    // Test 9: Integration test - POST /api/config/save through buildResponse routing
    {
        printf("Test 9: POST /api/config/save through buildResponse routing... ");
        HttpFileServer::Config config;
        config.documentRoot = "./www";
        TestFileServer server(ServerBind{"127.0.0.1", Port{18088}}, config);
        
        HttpClientState state;
        state.peerAddress = "127.0.0.1";
        state.request = "POST /api/config/save HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: 2\r\n"
                        "\r\n{}";
        state.parsedRequest = HttpRequest::parse(state.request);
        
        server.testBuildResponse(state);
        
        std::string response(state.dataView.data(), state.dataView.size());
        if (response.find("HTTP/1.1 200 OK") != std::string::npos) {
            printf("PASSED\n");
            passed++;
        } else {
            printf("FAILED (routing test failed, got: %s)\n", response.c_str());
            failed++;
        }
    }
    
    printf("\nConfig editor API tests: %d passed, %d failed\n", passed, failed);
    return failed;
}
