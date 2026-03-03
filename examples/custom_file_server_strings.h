#pragma once
// String literals for custom_file_server.cpp
// Separated to avoid auto-formatting corruption issues

namespace ServerStrings {

// Server startup messages
inline const char* HEADER = "=== Custom HTTP File Server Example ===\n";
inline const char* STARTING = "Custom HTTP File Server starting on http://localhost:8080/\n";
inline const char* SERVING_FROM = "Serving files from: ";

// Browser testing guide
inline const char* GUIDE_HEADER = "\n=== BROWSER TESTING GUIDE ===\n";
inline const char* AUTH_SECTION = "\n[AUTH] BASIC AUTHENTICATION:\n   URL: http://localhost:8080/\n   Username: admin\n   Password: secret\n   → Browser will show login dialog\n   → Check access.log for authentication attempts\n";
inline const char* DIR_SECTION = "\n[DIR] DIRECTORY LISTING:\n   URL: http://localhost:8080/\n   → Shows enhanced directory listing with file sizes and dates\n   → Click on subdirectories to navigate\n   → Click on files to view them (browser displays or downloads based on MIME type)\n";
inline const char* FILE_SECTION = "\n[FILE] FILE SERVING:\n   URL: http://localhost:8080/index.html\n   URL: http://localhost:8080/style.css\n   URL: http://localhost:8080/script.js\n   → Files are served with correct MIME types\n   → Check browser developer tools for headers\n";
inline const char* ACCESS_SECTION = "\n[BLOCK] ACCESS CONTROL:\n   URL: http://localhost:8080/config.conf\n   URL: http://localhost:8080/server.log\n   URL: http://localhost:8080/temp.tmp\n   → Should return 403 Forbidden\n   → Custom error page with styling\n";
inline const char* MIME_SECTION = "\n[MIME] CUSTOM MIME TYPES:\n   Create test files: test.wasm, test.ts, test.jsx, test.tsx\n   URL: http://localhost:8080/test.wasm\n   → Served as 'application/wasm'\n   URL: http://localhost:8080/test.ts\n   → Served as 'application/typescript'\n";
inline const char* PERF_SECTION = "\n[PERF] PERFORMANCE FEATURES:\n   1. ETag Support:\n      - Load any file twice\n      - Second request should return 304 Not Modified\n      - Check Network tab in browser dev tools\n   2. Last-Modified Headers:\n      - Check Response Headers in dev tools\n      - Should see 'Last-Modified' field\n";
inline const char* ERROR_SECTION = "\n[ERROR] ERROR HANDLING:\n   URL: http://localhost:8080/nonexistent.html\n   → Custom 404 error page with styling\n   URL: http://localhost:8080/../etc/passwd\n   → 404 in browser (URL normalized), 403 via curl --path-as-is\n   → curl --path-as-is http://localhost:8080/../etc/passwd -u admin:secret\n   URL: http://localhost:8080/ (with wrong auth)\n   → Custom 401 error page\n";
inline const char* DEVTOOLS_SECTION = "\n[DEBUG] DEVELOPER TOOLS TESTING:\n   1. Open Chrome DevTools (F12)\n   2. Network Tab:\n      - See all requests with status codes\n      - Check Response Headers for custom headers\n      - Verify ETag and Last-Modified headers\n   3. Console Tab:\n      - Should see no errors for valid files\n   4. Application/Storage Tab:\n      - Check if browser caches responses properly\n";
inline const char* LOG_SECTION = "\n[LOG] ACCESS LOG:\n   File: access.log (in same directory as server)\n   → Shows timestamp, method, path for each request\n   → Updates in real-time as you browse\n";
inline const char* CHECKLIST_SECTION = "\n[TEST] TESTING CHECKLIST:\n   □ Authentication prompt appears\n   □ Valid credentials grant access\n   □ Invalid credentials show 401 error\n   □ Directory listing shows file metadata\n   □ Files download with correct MIME types\n   □ Sensitive files return 403 errors\n   □ Non-existent files return 404 errors\n   □ Path traversal attempts are blocked\n   □ ETag headers work (304 responses on reload)\n   □ Access log records all requests\n   □ Custom headers appear in responses\n";
inline const char* FEATURES_SECTION = "\n[CONFIG]  SERVER FEATURES:\n  - Basic authentication (admin:secret)\n  - Access logging to access.log\n  - Enhanced directory listings with file info\n  - Custom error pages with CSS styling\n  - Access control (denies .conf, .log, .tmp files)\n  - Custom MIME types for modern web files\n  - ETag and Last-Modified support\n  - Security headers (X-Content-Type-Options, X-Frame-Options)\n  - Path traversal protection\n";
inline const char* PRESS_CTRL_C = "\nPress Ctrl+C to stop the server\n";

// Shutdown messages
inline const char* SERVER_STOPPED = "\n[OK] Server stopped.\n";
inline const char* LOG_SAVED = "[LOG] Access log saved to: access.log\n";
inline const char* THANK_YOU = "[BYE] Thank you for using HttpFileServer!\n";

} // namespace ServerStrings
