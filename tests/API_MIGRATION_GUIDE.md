# API Migration Guide: Exception-based to Result<T>

This guide shows how to migrate tests from the old exception-based API to the new `SocketFactory` API with `Result<T>`.

## Key Changes

### Old API (Exception-based)
```cpp
// Throws SocketException on failure
TcpSocket srv(AddressFamily::IPv4, ServerBind{"127.0.0.1", Port{8080}});
TcpSocket client(AddressFamily::IPv4, ConnectArgs{"example.com", Port{80}});
```

### New API (Result<T>)
```cpp
// Returns Result<T> - check isSuccess() or isError()
auto srv_result = SocketFactory::createTcpServer(ServerBind{"127.0.0.1", Port{8080}});
if (srv_result.isSuccess()) {
    auto& srv = srv_result.value();
    // Use srv
} else {
    std::cerr << "Error: " << srv_result.message() << std::endl;
}

auto client_result = SocketFactory::createTcpClient(
    AddressFamily::IPv4, ConnectArgs{"example.com", Port{80}});
if (client_result.isError()) {
    // Handle error
}
```

## Common Patterns

### 1. Socket Creation
```cpp
// OLD
TcpSocket sock;
if (!sock.isValid()) { /* error */ }

// NEW  
auto result = SocketFactory::createTcpSocket();
if (result.isError()) { /* error */ }
auto& sock = result.value();
```

### 2. Server Creation
```cpp
// OLD
try {
    TcpSocket srv(AddressFamily::IPv4, ServerBind{"127.0.0.1", Port{8080}});
    // Use srv
} catch (const SocketException& e) {
    std::cerr << "Error: " << e.what() << std::endl;
}

// NEW
auto srv_result = SocketFactory::createTcpServer(ServerBind{"127.0.0.1", Port{8080}});
if (srv_result.isError()) {
    std::cerr << "Error: " << srv_result.message() << std::endl;
    return;
}
auto& srv = srv_result.value();
```

### 3. Client Creation
```cpp
// OLD
try {
    TcpSocket client(AddressFamily::IPv4, ConnectArgs{"example.com", Port{80}});
    // Use client
} catch (const SocketException& e) {
    // Handle error
}

// NEW
auto client_result = SocketFactory::createTcpClient(
    AddressFamily::IPv4, ConnectArgs{"example.com", Port{80}});
if (client_result.isError()) {
    // Handle error
    return;
}
auto& client = client_result.value();
```

### 4. Error Checking
```cpp
// OLD
if (!sock.isValid()) {
    std::cerr << "Error: " << sock.getErrorMessage() << std::endl;
}

// NEW
if (result.isError()) {
    std::cerr << "Error: " << result.message() << std::endl;
}
```

## Test Migration Steps

1. **Replace constructors with SocketFactory methods**
2. **Wrap in Result<T> error checking**
3. **Replace try/catch with isSuccess()/isError()**
4. **Update error message access**
5. **Test both success and failure cases**

## Methods Available

```cpp
// Basic sockets
Result<TcpSocket> SocketFactory::createTcpSocket(AddressFamily family = AddressFamily::IPv4);
Result<UdpSocket> SocketFactory::createUdpSocket(AddressFamily family = AddressFamily::IPv4);

// Server sockets
Result<TcpSocket> SocketFactory::createTcpServer(AddressFamily family, const ServerBind& config);
Result<TcpSocket> SocketFactory::createTcpServer(const ServerBind& config); // IPv4 convenience

// Client sockets  
Result<TcpSocket> SocketFactory::createTcpClient(AddressFamily family, const ConnectArgs& config);
```

## Note on accept()

The `accept()` method still returns `std::unique_ptr<TcpSocket>` (not `Result<T>`) because it's a runtime operation, not a construction operation:

```cpp
auto client = srv.accept();  // Returns nullptr on error
if (client != nullptr) {
    // Use client
}
```

## Complete Example

See `test_socket_factory.cpp` for a complete working example of the new API.
