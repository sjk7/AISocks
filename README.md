# aiSocks - Cross-Platform Socket Library

A modern C++ socket library that abstracts platform differences between Windows, macOS, and Linux using the pimpl (Pointer to Implementation) idiom.

## Features

Zero dependencies, other than a standard C++ compiler and CMake.

- ✅ Cross-platform support (Windows, macOS, Linux)
- ✅ Clean pimpl-based API hiding platform details
- ✅ IPv4 and IPv6 support
- ✅ TCP and UDP socket support
- ✅ Blocking and non-blocking modes
- ✅ Server and client functionality
- ✅ CamelCase naming convention
- ✅ Modern C++17
- ✅ CMake build system

## Project Structure

```
AISOcks/
├── CMakeLists.txt          # Root CMake configuration
├── lib/                    # Socket library
│   ├── CMakeLists.txt      # Library CMake configuration
│   ├── include/
│   │   └── Socket.h        # Public API header
│   └── src/
│       ├── Socket.cpp      # Public API implementation
│       ├── SocketImpl.h    # Private implementation header
│       └── SocketImpl.cpp  # Platform-specific implementation
└── examples/
    └── main.cpp            # Example client-server application
```

## Building

### Windows
```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### macOS/Linux
```bash
mkdir build
cd build
cmake ..
make
```

## Running the Example

After building, run the example:

### Windows
```powershell
.\build\Release\aiSocksExample.exe
```

### macOS/Linux
```bash
./build/aiSocksExample
```

The example demonstrates a simple client-server communication where:
1. Server starts and listens on port 8080
2. Client connects to the server
3. Client sends "Hello from client!"
4. Server receives the message and responds with "Hello from server!"

## Usage

### Basic IPv4 Server

```cpp
#include "Socket.h"

using namespace aiSocks;

Socket serverSocket(SocketType::TCP, AddressFamily::IPv4);
serverSocket.setReuseAddress(true);
serverSocket.bind("0.0.0.0", 8080);
serverSocket.listen(5);

auto clientSocket = serverSocket.accept();
char buffer[1024];
int bytesReceived = clientSocket->receive(buffer, sizeof(buffer));
```

### Basic IPv4 Client

```cpp
#include "Socket.h"

using namespace aiSocks;

Socket clientSocket(SocketType::TCP, AddressFamily::IPv4);
clientSocket.connect("127.0.0.1", 8080);

const char* message = "Hello!";
clientSocket.send(message, strlen(message));
```

### IPv6 Server

```cpp
Socket serverSocket(SocketType::TCP, AddressFamily::IPv6);
serverSocket.setReuseAddress(true);
serverSocket.bind("::1", 8080);  // IPv6 loopback
serverSocket.listen(5);
```

### IPv6 Client

```cpp
Socket clientSocket(SocketType::TCP, AddressFamily::IPv6);
clientSocket.connect("::1", 8080);  // Connect to IPv6 loopback
```

## API Overview

### Socket Class

**Constructor:**
- `Socket(SocketType type = SocketType::TCP, AddressFamily family = AddressFamily::IPv4)` - Create a TCP or UDP socket with IPv4 or IPv6

**Server Operations:**
- `bool bind(const std::string& address, uint16_t port)` - Bind to an address/port
- `bool listen(int backlog = 10)` - Start listening for connections
- `std::unique_ptr<Socket> accept()` - Accept incoming connection

**Client Operations:**
- `bool connect(const std::string& address, uint16_t port)` - Connect to server

**Data Transfer:**
- `int send(const void* data, size_t length)` - Send data
- `int receive(void* buffer, size_t length)` - Receive data

**Socket Options:**
- `bool setBlocking(bool blocking)` - Set blocking/non-blocking mode
- `bool isBlocking() const` - Check if socket is in blocking mode
- `bool setReuseAddress(bool reuse)` - Enable SO_REUSEADDR
- `bool setTimeout(int seconds)` - Set receive timeout

**Utility:**
- `void close()` - Close the socket
- `bool isValid()` - Check if socket is valid
- `AddressFamily getAddressFamily() const` - Get the address family (IPv4/IPv6)
- `SocketError getLastError()` - Get last error code
- `std::string getErrorMessage()` - Get last error message

### Socket Types

- `SocketType::TCP` - Stream socket (SOCK_STREAM)
- `SocketType::UDP` - Datagram socket (SOCK_DGRAM)

### Address Families

- `AddressFamily::IPv4` - Internet Protocol version 4
- `AddressFamily::IPv6` - Internet Protocol version 6

### Error Codes

```cpp
enum class SocketError {
    None, CreateFailed, BindFailed, ListenFailed,
    AcceptFailed, ConnectFailed, SendFailed,
    ReceiveFailed, CloseFailed, SetOptionFailed,
    InvalidSocket, Timeout, WouldBlock, Unknown
};
```

## Platform Details

### Windows
- Uses Winsock2 (`ws2_32.lib`)
- Automatically initializes WSA on first socket creation

### macOS/Linux
- Uses BSD sockets
- POSIX-compliant implementation

## Requirements

- CMake 3.15 or higher
- C++17 compatible compiler
  - MSVC 2017 or higher (Windows)
  - GCC 7 or higher (Linux)
  - Clang 5 or higher (macOS)

## License

This is a demonstration library for educational purposes.

