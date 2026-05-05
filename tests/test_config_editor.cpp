// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests for HttpFileServer config editor API handlers

#include "HttpFileServer.h"
#include "test_helpers.h"

#include <string>

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
    
    // Test 2: handleGetCurrentConfig
    {
        printf("Test 2: handleGetCurrentConfig... ");
        HttpFileServer::Config config;
        config.documentRoot = "./www";
        config.enableLogging = true;
        TestFileServer server(ServerBind{"127.0.0.1", Port{18081}}, config);
        
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
    
    // Test 3: handleSaveConfig
    {
        printf("Test 3: handleSaveConfig... ");
        HttpFileServer::Config config;
        config.documentRoot = "./www";
        TestFileServer server(ServerBind{"127.0.0.1", Port{18082}}, config);
        
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
    
    // Test 4: Local-only access check (via isLocalClient helper)
    {
        printf("Test 4: Local-only access check... ");
        HttpFileServer::Config config;
        config.documentRoot = "./www";
        TestFileServer server(ServerBind{"127.0.0.1", Port{18083}}, config);
        
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
    
    printf("\nConfig editor API tests: %d passed, %d failed\n", passed, failed);
    return failed;
}
