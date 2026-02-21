// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
// Precompiled header for aiSocksLib
// Include expensive and frequently-used headers here to improve build times

#ifndef AISOCKS_PCH_H
#define AISOCKS_PCH_H

// Standard library headers used across multiple translation units
// Note: Avoid <chrono> to prevent conflicts with aiSocks::Milliseconds
#include <cstddef>
#include <cstdint>
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

// Platform-specific headers
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <iphlpapi.h>
#include <mmsystem.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#endif // AISOCKS_PCH_H
