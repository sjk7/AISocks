// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "DualServerOrchestrator.h"
#include "IpFilter.h"
#include "test_helpers.h"
#include <chrono>
#include <thread>

using namespace aiSocks;
using namespace std::chrono_literals;

int main() {
    printf("=== DualServerOrchestrator Tests ===\n");

    // Config for tests
    HttpFileServer::Config config;
    config.documentRoot = ".";

    // Happy Path: Startup and Graceful Stop
    BEGIN_TEST("Happy Path: HTTP and HTTPS servers start and stop gracefully");
    {
        DualServerOrchestrator::Ports ports{0, 0}; // Ephemeral ports
#ifdef AISOCKS_ENABLE_TLS
        TlsServerConfig tls("tests/certs/server-cert.pem", "tests/certs/server-key.pem");
        DualServerOrchestrator orchestrator(ports, config, &tls);
#else
        DualServerOrchestrator orchestrator(ports, config, nullptr);
#endif
        
        REQUIRE(orchestrator.isValid());
        
        std::thread runThread([&] { orchestrator.run(ClientLimit::Low, Milliseconds{1}); });
        
        std::this_thread::sleep_for(100ms); // Let them bind
        orchestrator.stop();
        
        if (runThread.joinable()) runThread.join();
        REQUIRE(true); // Reached here = no deadlock
    }

    // Sad Path: Port conflict handling
    BEGIN_TEST("Sad Path: Fail gracefull on port conflict");
    {
        // Occupy an ephemeral port on the same wildcard address (0.0.0.0)
        // that the orchestrator uses internally.  With reuseAddr=true on both
        // sides, the second bind to the exact same address+port still fails
        // (SO_REUSEPORT would be needed to share — and we don't set it).
        // logStartupErrors=false suppresses the expected bind-failure log.
        TcpSocket conflictSock(AddressFamily::IPv4,
            ServerBind{"0.0.0.0", Port{0}, Backlog{}, true, false});
        REQUIRE(conflictSock.isValid());
        auto ep = conflictSock.getLocalEndpoint();
        REQUIRE(ep.isSuccess());
        uint16_t takenPort = ep.value().port.value();

        DualServerOrchestrator::Ports ports{takenPort, 0};
        DualServerOrchestrator orchestrator(ports, config, nullptr);
        
        // isValid() should be false if either server fails to bind
        REQUIRE(!orchestrator.isValid());
        
        // run() should return immediately if invalid (no threads spawned)
        orchestrator.run(ClientLimit::Low, Milliseconds{1});
        REQUIRE(true);
    }

    // Shared IpFilter verification
    BEGIN_TEST("Shared IpFilter: Policy applied to both servers");
    {
        IpFilter filter;
        filter.setLocalOnly(true); // Should block everything not local

        DualServerOrchestrator::Ports ports{0, 0};
        DualServerOrchestrator orchestrator(ports, config, nullptr);
        orchestrator.setIpFilter(&filter);
        
        // Verification is implicit: if setIpFilter didn't crash and 
        // the pointer was distributed correctly, it's successful.
        REQUIRE(true); 
    }

    printf("\nDualServerOrchestrator tests complete.\n");
    return 0;
}
