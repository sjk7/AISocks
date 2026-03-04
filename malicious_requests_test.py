#!/usr/bin/env python3
"""
Security Testing Script - Tests server resilience against malicious requests
"""
import socket
import time
import sys

HOST = "127.0.0.1"
PORT = 8080

def send_request(name, request_data, timeout=2):
    """Send a request and capture the response"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((HOST, PORT))
        
        if isinstance(request_data, str):
            request_data = request_data.encode('latin-1')
        
        sock.sendall(request_data)
        response = sock.recv(4096).decode('latin-1', errors='ignore')
        sock.close()
        
        status_line = response.split('\r\n')[0] if response else "NO RESPONSE"
        print(f"✓ {name:45} -> {status_line[:60]}")
        return True
    except socket.timeout:
        print(f"⏱ {name:45} -> TIMEOUT (server may have rate limited)")
        return False
    except Exception as e:
        print(f"✗ {name:45} -> ERROR: {str(e)[:40]}")
        return False

def run_tests():
    print("=" * 80)
    print("MALICIOUS REQUEST SECURITY TESTING")
    print("=" * 80)
    print()
    
    tests = [
        # Path Traversal Attacks
        ("Path Traversal: ../../../etc/passwd",
         "GET /../../../etc/passwd HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        ("Path Traversal: URL encoded",
         "GET /%2e%2e%2f%2e%2e%2f%2e%2e%2fetc/passwd HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        ("Path Traversal: Windows style",
         "GET /..\\..\\..\\windows\\system32\\config\\sam HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        ("Path Traversal: Double encoding",
         "GET /%252e%252e%252f%252e%252e%252f HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        # Oversized/Malformed Requests
        ("Extremely Long URL (10KB)",
         f"GET /{'A' * 10000} HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        ("Extremely Long Header (20KB)",
         f"GET / HTTP/1.1\r\nHost: localhost\r\nX-Custom: {'B' * 20000}\r\n\r\n"),
        
        ("Too Many Headers (500 headers)",
         "GET / HTTP/1.1\r\nHost: localhost\r\n" + 
         "".join([f"X-Header{i}: value{i}\r\n" for i in range(500)]) + "\r\n"),
        
        # Null Byte Injection
        ("Null Byte in URL",
         b"GET /index.html\x00.jpg HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        ("Null Byte in Header",
         b"GET / HTTP/1.1\r\nHost: localhost\x00evil.com\r\n\r\n"),
        
        # HTTP Request Smuggling Attempts
        ("Content-Length + Transfer-Encoding",
         "GET / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 10\r\n"
         "Transfer-Encoding: chunked\r\n\r\n"),
        
        ("Duplicate Content-Length",
         "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n"
         "Content-Length: 100\r\n\r\n"),
        
        # Header Injection
        ("CRLF Injection in Header",
         "GET / HTTP/1.1\r\nHost: localhost\r\nX-Custom: value\r\n"
         "X-Injected: injected\r\n\r\n"),
        
        # Invalid HTTP Methods
        ("Invalid HTTP Method: TRACE",
         "TRACE / HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        ("Invalid HTTP Method: TRACK",
         "TRACK / HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        ("Invalid HTTP Method: PROPFIND",
         "PROPFIND / HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        ("No HTTP Method",
         "/ HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        # Malformed HTTP
        ("No HTTP Version",
         "GET /\r\nHost: localhost\r\n\r\n"),
        
        ("Invalid HTTP Version",
         "GET / HTTP/9.9\r\nHost: localhost\r\n\r\n"),
        
        ("No Host Header",
         "GET / HTTP/1.1\r\n\r\n"),
        
        ("Spaces in URL",
         "GET /path with spaces/file.txt HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        # Command Injection Attempts (in query string)
        ("Command Injection: Semicolon",
         "GET /index.html?cmd=ls;cat%20/etc/passwd HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        ("Command Injection: Pipe",
         "GET /index.html?cmd=|cat%20/etc/passwd HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        ("Command Injection: Backticks",
         "GET /index.html?cmd=`whoami` HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        # XXE/XML Injection (if server processes XML)
        ("XML Entity Injection",
         "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/xml\r\n"
         "Content-Length: 100\r\n\r\n"
         "<?xml version='1.0'?><!DOCTYPE foo [<!ENTITY xxe SYSTEM 'file:///etc/passwd'>]><foo>&xxe;</foo>"),
        
        # Slowloris-style (partial request)
        ("Partial Request (no ending)",
         "GET / HTTP/1.1\r\nHost: localhost\r\nX-Custom: "),
        
        # Unicode/Special Characters
        ("Unicode in URL",
         "GET /\u202e\u202d HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        ("Special Chars in URL",
         "GET /<script>alert(1)</script> HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        
        # HTTP Response Splitting
        ("Response Splitting Attempt",
         "GET /index.html HTTP/1.1\r\nHost: localhost\r\n"
         "X-Custom: value\r\n\r\nHTTP/1.1 200 OK\r\n\r\n"),
    ]
    
    print(f"Running {len(tests)} security tests...\n")
    
    passed = 0
    failed = 0
    timeout = 0
    
    for name, request in tests:
        result = send_request(name, request)
        if result:
            passed += 1
        elif "TIMEOUT" in str(result):
            timeout += 1
        else:
            failed += 1
        time.sleep(0.1)  # Small delay between tests
    
    print()
    print("=" * 80)
    print(f"RESULTS: {passed} handled, {timeout} timeouts, {failed} errors")
    print("=" * 80)
    
    # Test if server is still alive
    print("\nFinal Check: Testing if server is still responsive...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(2)
    try:
        sock.connect((HOST, PORT))
        sock.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
        response = sock.recv(1024)
        sock.close()
        print("✓ SERVER STILL ALIVE AND RESPONDING!")
    except Exception as e:
        print(f"✗ SERVER MAY HAVE CRASHED: {e}")

if __name__ == "__main__":
    try:
        run_tests()
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        sys.exit(1)
