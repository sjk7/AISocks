// Test to demonstrate ClientHttpRequest efficiency
#include "lib/include/ClientHttpRequest.h"
#include <iostream>
#include <chrono>

using namespace aiSocks;

int main() {
    std::cout << "=== ClientHttpRequest Efficiency Test ===\n\n";
    
    // Test 1: Zero-copy URL parsing
    std::cout << "1. Testing zero-copy URL parsing:\n";
    std::string url = "http://example.com:8080/path/to/resource?query=value";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Build request - all parsing uses string_views into the original URL
    std::string request = ClientHttpRequest::builder()
        .method("GET")
        .url(url)
        .header("User-Agent", "TestClient/1.0")
        .header("Accept", "application/json")
        .build();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "   Request built in " << duration.count() << " microseconds\n";
    std::cout << "   Request size: " << request.size() << " bytes\n";
    std::cout << "   First 200 chars: " << request.substr(0, 200) << "...\n\n";
    
    // Test 2: POST with body
    std::cout << "2. Testing POST with body:\n";
    std::string jsonBody = R"({"name":"John","age":30,"city":"New York"})";
    
    start = std::chrono::high_resolution_clock::now();
    
    std::string postRequest = ClientHttpRequest::builder()
        .method("POST")
        .url("http://api.example.com/users")
        .header("Content-Type", "application/json")
        .header("Authorization", "Bearer token123")
        .body(jsonBody)
        .build();
    
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "   POST request built in " << duration.count() << " microseconds\n";
    std::cout << "   Request size: " << postRequest.size() << " bytes\n";
    std::cout << "   Contains JSON body: " << (postRequest.find("John") != std::string::npos ? "YES" : "NO") << "\n\n";
    
    // Test 3: Performance with many requests
    std::cout << "3. Performance test - 1000 requests:\n";
    start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000; ++i) {
        std::string testUrl = "http://test" + std::to_string(i) + ".example.com/api/endpoint/" + std::to_string(i);
        ClientHttpRequest::builder()
            .method("GET")
            .url(testUrl)
            .header("X-Request-ID", std::to_string(i))
            .build();
    }
    
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "   1000 requests built in " << duration.count() << " milliseconds\n";
    std::cout << "   Average: " << static_cast<double>(duration.count()) / 1000.0 << " ms per request\n\n";
    
    std::cout << "=== Efficiency Test Complete ===\n";
    std::cout << "\nKey optimizations:\n";
    std::cout << "- Zero-copy URL parsing using string_view\n";
    std::cout << "- No string allocations during URL parsing\n";
    std::cout << "- StringBuilder for efficient request building\n";
    std::cout << "- All string_views reference original buffer\n";
    
    return 0;
}
