# Advanced File Server Examples

This document provides usage examples for the advanced_file_server with configuration file support and TLS options.

## Configuration File

Create a `server.conf` file in the same directory as the server:

```
# Server configuration
bind_address = 0.0.0.0
http_port = 8080
https_port = 8443
www_root = ./www
cert = server-cert.pem
key = server-key.pem
enable_http = true
enable_https = false
index_file = index.html
directory_listing = true
log_path = access.log
enable_logging = true
enable_log_rotation = true
log_max_size = 10MB
log_max_files = 5
```

## Command-Line Options

- `--config FILE` - Load configuration from specified file
- `--bind ADDR` - Bind address (e.g., 127.0.0.1, 0.0.0.0)
- `--port PORT` - HTTP port (default: 8080)
- `--https PORT` - Enable HTTPS on specified port
- `--cert FILE` - TLS certificate file path
- `--key FILE` - TLS private key file path
- `--root PATH` - Document root directory
- `--log-path PATH` - Log file path (default: access.log)
- `--no-log` - Disable logging

**Important:** Command-line arguments always override config file settings. The precedence order is:
1. Config file defaults
2. Command-line arguments (highest priority)

## Usage Examples

### Basic HTTP Server

```bash
# Default settings (port 8080, bind 0.0.0.0, www/ as root)
./build-relwithdebinfo/advanced_file_server

# Custom port
./build-relwithdebinfo/advanced_file_server --port 9090

# Custom root directory
./build-relwithdebinfo/advanced_file_server --root /var/www

# Bind to localhost only
./build-relwithdebinfo/advanced_file_server --bind 127.0.0.1
```

### Using Configuration File

```bash
# Load from server.conf (auto-detected if present)
./build-relwithdebinfo/advanced_file_server

# Load from custom config file
./build-relwithdebinfo/advanced_file_server --config /etc/server.conf
```

### HTTPS Server

```bash
# Enable HTTPS on port 8443
./build-relwithdebinfo/advanced_file_server --https 8443 --cert server-cert.pem --key server-key.pem

# HTTPS with custom bind address
./build-relwithdebinfo/advanced_file_server --https 8443 --cert server-cert.pem --key server-key.pem --bind 192.168.1.100
```

### Logging Configuration

```bash
# Custom log file path
./build-relwithdebinfo/advanced_file_server --log-path /var/log/server.log

# Disable logging entirely
./build-relwithdebinfo/advanced_file_server --no-log
```

### Log Rotation

The server supports size-based log rotation to prevent log files from growing too large. When enabled, logs are rotated when they exceed the configured size limit.

**Config file options:**
```
enable_log_rotation = true
log_max_size = 10MB
log_max_files = 5
```

**Size suffixes supported:** KB, MB, GB

**Rotation behavior:**
- When `access.log` exceeds `log_max_size`, it's renamed to `access.log.1`
- Existing `access.log.1` becomes `access.log.2`, and so on
- Oldest files beyond `log_max_files` are deleted
- A new empty `access.log` is created

**Example config:**
```
# Rotate logs at 50MB, keep 10 rotated files
enable_log_rotation = true
log_max_size = 50MB
log_max_files = 10
```

### Combined Examples

```bash
# Full configuration via command line
./build-relwithdebinfo/advanced_file_server \
    --bind 0.0.0.0 \
    --port 8080 \
    --root ./www \
    --log-path /var/log/server.log

# HTTPS with custom root and logging
./build-relwithdebinfo/advanced_file_server \
    --https 8443 \
    --cert /etc/ssl/certs/server.pem \
    --key /etc/ssl/private/server.key \
    --root /var/www \
    --log-path /var/log/https-access.log
```

### Legacy Positional Arguments

```bash
# Legacy format still supported: [port] [root]
./build-relwithdebinfo/advanced_file_server 9090 /var/www
```

## Notes

- Command-line arguments override config file settings
- If `server.conf` exists in the current directory, it's loaded automatically
- TLS support requires building with `AISOCKS_ENABLE_TLS=ON`
- The server supports Apache Combined Log Format with User-Agent and Referer headers
- Authentication credentials: username `admin`, password `secret`
