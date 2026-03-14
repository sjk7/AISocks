// Bridge OpenSSL BIO FILE* usage to the MSVC runtime on Windows.
// This must be compiled exactly once per executable that uses OpenSSL FILE APIs.
#if defined(_MSC_VER) && defined(AISOCKS_ENABLE_TLS)
#include <openssl/applink.c>
#endif
