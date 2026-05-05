# Log Rotation

## Overview

The `LogRotation` class provides size-based log file rotation for HTTP file servers. When a log file exceeds a configured size limit, it is automatically rotated to prevent unbounded disk usage.

## Configuration

Log rotation is configured via `HttpFileServer::Config`:

```cpp
HttpFileServer::Config config;
config.logPath = "access.log";              // Log file path
config.enableLogging = true;                // Enable/disable logging
config.logRotation.enabled = true;          // Enable/disable rotation
config.logRotation.maxSizeBytes = 10 * 1024 * 1024;  // 10MB
config.logRotation.maxFiles = 5;            // Keep 5 rotated files
```

### Config File Options

When using `ServerConf`, these options can be set in the config file:

```
log_path = access.log
enable_logging = true
enable_log_rotation = true
log_max_size = 10MB
log_max_files = 5
```

**Size suffixes supported:** `KB`, `MB`, `GB`

## How Rotation Works

When the current log file exceeds `maxSizeBytes`:

1. The log file is **closed** (no more writes)
2. Files are rotated in reverse order:
   - `access.log.4` → `access.log.5`
   - `access.log.3` → `access.log.4`
   - `access.log.2` → `access.log.3`
   - `access.log.1` → `access.log.2`
   - `access.log` → `access.log.1`
3. If `maxFiles` is exceeded, the oldest file is deleted
4. A new empty `access.log` is created
5. The `onLogRotate()` callback is invoked with the rotated file path

## Safety Guarantees

### File Closure Before Callback

The `onLogRotate()` callback is **only invoked after**:
- The log file has been **closed** (no active file handles)
- The file has been **renamed** to its rotated path (e.g., `access.log.1`)

This ensures the file is safe for compression or other post-processing operations without race conditions.

### Thread Safety

Log rotation is performed synchronously during the logging flow. This means:
- Rotation happens before the next log entry is written
- No concurrent writes to the rotated file can occur
- The callback is invoked in the same thread as the logging operation

## Using the Callback

### Override `onLogRotate()` in Derived Classes

```cpp
class MyFileServer : public HttpFileServer {
    protected:
    void onLogRotate(const std::string& rotatedFilePath) override {
        // Compress the rotated file
        system(("gzip " + rotatedFilePath).c_str());
        
        // Or perform any other post-rotation processing
        printf("Log rotated: %s\n", rotatedFilePath.c_str());
    }
};
```

### Example: Compression

```cpp
void onLogRotate(const std::string& rotatedFilePath) override {
#ifdef _WIN32
    // Windows: use PowerShell or native tools
    std::string cmd = "powershell Compress-Archive -Path \"" + 
                      rotatedFilePath + "\" -DestinationPath \"" + 
                      rotatedFilePath + ".zip\"";
    system(cmd.c_str());
#else
    // POSIX: use gzip
    system(("gzip " + rotatedFilePath).c_str());
#endif
}
```

## Configuring via ServerConf

```cpp
ServerConf conf = loadServerConf("server.conf");

HttpFileServer::Config config;
config.documentRoot = conf.wwwRoot;
config.logPath = conf.logPath;
config.enableLogging = conf.enableLogging;
config.logRotation.enabled = conf.enableLogRotation;
config.logRotation.maxSizeBytes = conf.logMaxSizeBytes;
config.logRotation.maxFiles = conf.logMaxFiles;

HttpFileServer server(bind, config);
```

## Default Values

- `logPath`: `"access.log"`
- `enableLogging`: `false`
- `logRotation.enabled`: `true`
- `logRotation.maxSizeBytes`: `10 * 1024 * 1024` (10 MB)
- `logRotation.maxFiles`: `5`

## Testing

The log rotation functionality is tested in `tests/test_log_rotation.cpp`:

```bash
./build-relwithdebinfo/tests/test_log_rotation
```

Tests cover:
- Configuration defaults
- Disabled rotation
- Empty file handling
- Basic rotation
- Max files limit enforcement
- Callback invocation

## Integration with HttpFileServer

All `HttpFileServer` instances automatically support log rotation:

```cpp
// Basic usage with rotation enabled
HttpFileServer::Config config;
config.logPath = "access.log";
config.enableLogging = true;
config.logRotation.enabled = true;
config.logRotation.maxSizeBytes = 50 * 1024 * 1024;  // 50MB

HttpFileServer server(bind, config);
```

Derived classes can override:
- `logRequest()` - for custom log formats
- `onLogRotate()` - for post-rotation processing (compression, etc.)
