// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

// Bridge OpenSSL BIO FILE* usage to the MSVC runtime on Windows.
// This must be compiled exactly once per executable that uses OpenSSL FILE
// APIs.
#if defined(_MSC_VER) && defined(AISOCKS_ENABLE_TLS)
#include <openssl/applink.c>
#endif
