// Fuzz target for the full request-processing pipeline:
//   raw bytes → HttpRequest::parse() → urlDecodePath(path) → resolveFilePath()
//
// This catches interaction bugs that per-layer fuzz targets miss — e.g. a path
// that looks safe after URL-decoding but still escapes documentRoot, or a
// request that parses cleanly but produces a malformed path for the resolver.
//
// Build:  cmake --build build-fuzz --target fuzz_pipeline
// Run:    ./build-fuzz/tests/fuzz_pipeline fuzz_corpus/pipeline -max_len=1024

#include "HttpFileServer.h"
#include "HttpRequest.h"
#include "ServerTypes.h"
#include "UrlCodec.h"
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
    void buildResponse(HttpClientState&) override {}
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Server constructed once — one socket bind at process start, then reused.
    static FuzzHttpFileServer server = [] {
        HttpFileServer::Config cfg;
        cfg.documentRoot = "/tmp/fuzz_root";
        return FuzzHttpFileServer{ServerBind{"127.0.0.1", Port{0}}, cfg};
    }();

    // Stage 1: parse the raw bytes as an HTTP request.
    auto req = HttpRequest::parse({reinterpret_cast<const char*>(data), size});
    if (!req.valid) return 0; // Reject unparseable inputs from corpus.

    // Stage 2: URL-decode the path component.
    std::string decoded = urlDecodePath(req.path);

    // Stage 3: resolve to a filesystem path — must never escape documentRoot.
    (void)server.fuzzResolvePath(decoded);
    return 0;
}
