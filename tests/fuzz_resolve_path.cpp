// Fuzz target for HttpFileServer::resolveFilePath().
//
// resolveFilePath() is the path-traversal defence boundary: it maps a URL
// target (already URL-decoded) onto a documentRoot-relative filesystem path.
// Any input that escapes documentRoot is a security bug.
//
// Note: the server is constructed ONCE as a static (one socket bind at
// process start). C++ constructor virtual dispatch fires in base-class order,
// so overriding binding in a subclass constructor is not possible — the static
// approach achieves the same goal with zero per-iteration overhead.
//
// Build:  cmake --build build-fuzz --target fuzz_resolve_path
// Run:    ./build-fuzz/tests/fuzz_resolve_path -max_len=1024

#include "HttpFileServer.h"
#include "ServerTypes.h"
#include <cstddef>
#include <cstdint>
#include <string>

using namespace aiSocks;

namespace {

class FuzzHttpFileServer : public HttpFileServer {
    public:
    using HttpFileServer::HttpFileServer;

    std::string fuzzResolvePath(const std::string& target) const {
        return resolveFilePath(target);
    }

    protected:
    // No-op: fuzz target never calls run(), so buildResponse is unreachable.
    void buildResponse(HttpClientState&) override {}
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Constructed once at first iteration — one real bind, then reused.
    static FuzzHttpFileServer server = [] {
        HttpFileServer::Config cfg;
        cfg.documentRoot = "/tmp/fuzz_root";
        return FuzzHttpFileServer{ServerBind{"127.0.0.1", Port{0}}, cfg};
    }();

    std::string target{reinterpret_cast<const char*>(data), size};
    (void)server.fuzzResolvePath(target);
    return 0;
}
