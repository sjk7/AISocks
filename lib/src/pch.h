// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
// Precompiled header for aiSocksLib
// Include expensive and frequently-used headers here to improve build times

#ifndef AISOCKS_PCH_H
#define AISOCKS_PCH_H

// Standard library headers used across multiple translation units
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <limits>
#include <cassert>
#include <cstring>
#include <mutex>

// Platform-specific headers
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <iphlpapi.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

// Project headers included by most translation units
// Include SocketTypes.h and Socket.h which are the most expensive project headers
#include "SocketTypes.h"
#include "Socket.h"
#include "SocketImpl.h"

#endif // AISOCKS_PCH_H
