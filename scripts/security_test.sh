#!/bin/bash

# Security Testing Script for HttpFileServer
echo "=== HTTP SERVER SECURITY ATTACK SIMULATION ==="
echo "Target: http://localhost:8080"
echo ""

# Test 1: Basic connectivity
echo "1. Testing basic connectivity..."
curl -s -o /dev/null -w "Status: %{http_code}\n" "http://localhost:8080/" || echo "Server not running"
echo ""

# Test 2: Path Traversal Attacks
echo "2. Path Traversal Attacks..."
echo "   Basic ../ attack:"
curl -s --path-as-is -o /dev/null -w "Status: %{http_code}\n" "http://localhost:8080/../../../etc/passwd" 2>/dev/null || echo "Failed"

echo "   URL-encoded attack:"
curl -s -o /dev/null -w "Status: %{http_code}\n" "http://localhost:8080/%2e%2e%2f%2e%2e%2f%2e%2e%2fetc%2fpasswd" 2>/dev/null || echo "Failed"

echo "   Double encoded attack:"
curl -s -o /dev/null -w "Status: %{http_code}\n" "http://localhost:8080/%252e%252e%252fetc%252fpasswd" 2>/dev/null || echo "Failed"
echo ""

# Test 3: HTTP Method Attacks
echo "3. HTTP Method Attacks..."
for method in POST PUT DELETE TRACE OPTIONS PATCH; do
    echo "   Testing $method:"
    curl -s -X $method -o /dev/null -w "Status: %{http_code}\n" "http://localhost:8080/" 2>/dev/null || echo "Failed"
done
echo ""

# Test 4: Header Injection Attacks
echo "4. Header Injection Attacks..."
curl -s -H "Host: evil.com" -o /dev/null -w "Status: %{http_code}\n" "http://localhost:8080/" 2>/dev/null || echo "Failed"
curl -s -H "User-Agent: <script>alert('XSS')</script>" -o /dev/null -w "Status: %{http_code}\n" "http://localhost:8080/" 2>/dev/null || echo "Failed"
curl -s -H "Referer: javascript:alert(1)" -o /dev/null -w "Status: %{http_code}\n" "http://localhost:8080/" 2>/dev/null || echo "Failed"
echo ""

# Test 5: Large Request Attacks
echo "5. Large Request Attacks..."
curl -s -H "Content-Length: 999999999" -o /dev/null -w "Status: %{http_code}\n" "http://localhost:8080/" 2>/dev/null || echo "Failed"
curl -s -H "Expect: 100-continue" -o /dev/null -w "Status: %{http_code}\n" "http://localhost:8080/" 2>/dev/null || echo "Failed"
echo ""

# Test 6: XSS Attempts
echo "6. XSS Attempts..."
curl -s "http://localhost:8080/<script>alert('XSS')</script>" -o /dev/null -w "Status: %{http_code}\n" 2>/dev/null || echo "Failed"
curl -s "http://localhost:8080/javascript:alert(1)" -o /dev/null -w "Status: %{http_code}\n" 2>/dev/null || echo "Failed"
echo ""

# Test 7: Information Disclosure
echo "7. Information Disclosure..."
curl -s "http://localhost:8080/" -I 2>/dev/null | grep -i server || echo "No server header"
curl -s "http://localhost:8080/nonexistent" -o /dev/null -w "Status: %{http_code}\n" 2>/dev/null || echo "Failed"
echo ""

echo "=== SECURITY TEST COMPLETE ==="
