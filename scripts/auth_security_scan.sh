#!/bin/bash

# Enhanced Security Testing Script for HttpFileServer with Authentication Support
HOST=${1:-127.0.0.1}
PORT=${2:-8080}
USER=${3:-"admin"}
PASS=${4:-"secret"}
AUTH="$USER:$PASS"
TARGET="http://$HOST:$PORT"

echo "=== AUTHENTICATED HTTP SERVER SECURITY SCAN ==="
echo "Target: $TARGET"
echo "Auth: $AUTH"
echo ""

# Helper function for curl
scan_curl() {
    local label=$1
    shift
    echo -n "   $label: "
    curl -s --user "$AUTH" -o /dev/null -w "Status: %{http_code}\n" "$@"
}

# Test 1: Connectivity
echo "1. Testing Basic Connectivity..."
scan_curl "Root Access" "$TARGET/"
echo ""

# Test 2: Path Traversal
echo "2. Path Traversal Attacks..."
scan_curl "Basic ../ attack" --path-as-is "$TARGET/../../../etc/passwd"
scan_curl "URL-encoded" "$TARGET/%2e%2e%2f%2e%2e%2f%2e%2e%2fetc%2fpasswd"
scan_curl "Double encoded" "$TARGET/%252e%252e%252fetc%252fpasswd"
scan_curl "OS Config" --path-as-is "$TARGET/../../../../etc/shadow"
echo ""

# Test 3: HTTP Method Injection
echo "3. HTTP Method Attacks..."
for method in POST PUT DELETE TRACE OPTIONS PATCH; do
    scan_curl "Method $method" -X $method "$TARGET/"
done
echo ""

# Test 4: Header Injection & XSS
echo "4. Injection Attacks..."
scan_curl "Host Injection" -H "Host: evil.com" "$TARGET/"
scan_curl "UA XSS" -H "User-Agent: <script>alert('XSS')</script>" "$TARGET/"
scan_curl "Referer XSS" -H "Referer: javascript:alert(1)" "$TARGET/"
scan_curl "Path XSS" "$TARGET/<script>alert(1)</script>"
echo ""

# Test 5: Resource Exhaustion (Light)
echo "5. Resource Handling..."
scan_curl "Large Content-Length" -H "Content-Length: 999999999" "$TARGET/"
scan_curl "Expect-100" -H "Expect: 100-continue" "$TARGET/"
echo ""

# Test 6: Information Disclosure
echo "6. Information Disclosure..."
echo -n "   Server Header: "
curl -s -u "$AUTH" -I "$TARGET/" | grep -i "^Server:" || echo "None (Good)"
scan_curl "404 Page" "$TARGET/this_file_is_missing_xyz"
scan_curl "Blocked file (.conf)" "$TARGET/config.conf"
scan_curl "Blocked file (.log)" "$TARGET/access.log"
echo ""

echo "=== SCAN COMPLETE ==="
