// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests for IpFilter: static config rules (exact, CIDR, wildcard, localonly)
// and the auto-blacklist rate-limiting behaviour.

#include "IpFilter.h"
#include "FileIO.h"
#include "test_helpers.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#define unlink(p) DeleteFileA(p)
#else
#include <unistd.h>
#endif

using namespace aiSocks;
using namespace std::chrono_literals;

namespace {
constexpr auto kFastBlacklistDuration = std::chrono::milliseconds{80};
constexpr auto kFastRateWindow = std::chrono::milliseconds{80};
constexpr auto kFastSleepBuffer = std::chrono::milliseconds{120};
} // namespace

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char* kCfgPath = "test_ipfilter_tmp.conf";

static void writeConfig(const char* content) {
    File f(kCfgPath, "w");
    if (!f) return;
    f.writeString(content);
    f.flush();
}

static void removeConfig() {
    unlink(kCfgPath);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    printf("=== IpFilter Tests ===\n");

    // ------------------------------------------------------------------
    BEGIN_TEST("exact IP: matching address is blocked");
    {
        IpFilter f;
        writeConfig("Require not ip 192.0.2.5\n");
        REQUIRE(f.loadConfig(kCfgPath));
        REQUIRE(!f.isAllowed("192.0.2.5"));
        REQUIRE(f.isAllowed("192.0.2.6"));
        REQUIRE(f.isAllowed("10.0.0.1"));
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("CIDR /24: all IPs in block are blocked");
    {
        IpFilter f;
        writeConfig("Require not ip 198.51.100.0/24\n");
        REQUIRE(f.loadConfig(kCfgPath));
        REQUIRE(!f.isAllowed("198.51.100.0"));
        REQUIRE(!f.isAllowed("198.51.100.1"));
        REQUIRE(!f.isAllowed("198.51.100.255"));
        REQUIRE(f.isAllowed("198.51.101.0"));
        REQUIRE(f.isAllowed("10.0.0.1"));
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("CIDR /21: block from the original ticket");
    {
        IpFilter f;
        writeConfig("Require not ip 5.34.240.0/21\n");
        REQUIRE(f.loadConfig(kCfgPath));
        // 5.34.240.0 – 5.34.247.255 should all be blocked
        REQUIRE(!f.isAllowed("5.34.240.0"));
        REQUIRE(!f.isAllowed("5.34.247.255"));
        REQUIRE(f.isAllowed("5.34.248.0"));
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("wildcard .*: blocks matching prefix");
    {
        IpFilter f;
        writeConfig("Require not ip 92.40.*\n");
        REQUIRE(f.loadConfig(kCfgPath));
        REQUIRE(!f.isAllowed("92.40.0.1"));
        REQUIRE(!f.isAllowed("92.40.215.169"));
        REQUIRE(!f.isAllowed("92.40.255.255"));
        REQUIRE(f.isAllowed("92.41.0.1"));
        REQUIRE(f.isAllowed("10.0.0.1"));
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("multiple rules in one config");
    {
        IpFilter f;
        writeConfig("# comment\n"
                    "Require not ip 5.34.240.0/21\n"
                    "Require not ip 92.40.215.169\n"
                    "Require not ip 92.40.*\n");
        REQUIRE(f.loadConfig(kCfgPath));
        REQUIRE(!f.isAllowed("5.34.244.1"));
        REQUIRE(!f.isAllowed("92.40.215.169"));
        REQUIRE(!f.isAllowed("92.40.1.1"));
        REQUIRE(f.isAllowed("1.2.3.4"));
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("configRuleCount reflects loaded rules");
    {
        IpFilter f;
        writeConfig("Require not ip 1.2.3.4\n"
                    "Require not ip 10.0.0.0/8\n");
        f.loadConfig(kCfgPath);
        REQUIRE(f.configRuleCount() == 2);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("localonly: blocks non-loopback non-private");
    {
        IpFilter f;
        writeConfig("Require localonly\n");
        REQUIRE(f.loadConfig(kCfgPath));
        REQUIRE(f.isLocalOnly());
        // loopback → allowed
        REQUIRE(f.isAllowed("127.0.0.1"));
        REQUIRE(f.isAllowed("127.128.0.1"));
        // private RFC-1918 → allowed
        REQUIRE(f.isAllowed("10.0.0.1"));
        REQUIRE(f.isAllowed("172.16.0.1"));
        REQUIRE(f.isAllowed("172.31.255.254"));
        REQUIRE(f.isAllowed("192.168.1.50"));
        // public → blocked
        REQUIRE(!f.isAllowed("8.8.8.8"));
        REQUIRE(!f.isAllowed("1.1.1.1"));
        REQUIRE(!f.isAllowed("203.0.113.5"));
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("localonly: false by default");
    {
        IpFilter f;
        REQUIRE(!f.isLocalOnly());
        REQUIRE(f.isAllowed("8.8.8.8")); // public allowed by default
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("comments and blank lines are ignored");
    {
        IpFilter f;
        writeConfig("# this is a comment\n"
                    "\n"
                    "   # indented comment\n"
                    "Require not ip 1.2.3.4  # inline comment\n");
        REQUIRE(f.loadConfig(kCfgPath));
        REQUIRE(!f.isAllowed("1.2.3.4"));
        REQUIRE(f.isAllowed("1.2.3.5"));
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("setLocalOnly: programmatic override");
    {
        IpFilter f;
        f.setLocalOnly(true);
        REQUIRE(!f.isAllowed("5.5.5.5"));
        REQUIRE(f.isAllowed("192.168.0.1"));
        f.setLocalOnly(false);
        REQUIRE(f.isAllowed("5.5.5.5"));
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("loadConfig returns false for missing file");
    {
        IpFilter f;
        REQUIRE(!f.loadConfig("/nonexistent/path/that/cannot/exist.conf"));
        REQUIRE(f.configRuleCount() == 0);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("auto-blacklist: IP blocked after threshold requests");
    {
        IpFilter f;
        // Use a very low threshold (5 req / 60 s) for testing
        f.setAutoBlacklistThreshold(5);
        f.setAutoBlacklistWindow(std::chrono::seconds{60});
        f.setAutoBlacklistDuration(std::chrono::seconds{5400});

        const std::string ip = "198.51.100.7";
        REQUIRE(f.isAllowed(ip));

        for (int i = 0; i < 4; ++i) f.recordRequest(ip);
        REQUIRE(f.isAllowed(ip)); // under threshold
        REQUIRE(!f.isAutoBlacklisted(ip));

        f.recordRequest(ip); // 5th request → threshold hit
        REQUIRE(!f.isAllowed(ip));
        REQUIRE(f.isAutoBlacklisted(ip));
        REQUIRE(f.autoBlacklistSize() == 1);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("auto-blacklist: different IPs tracked independently");
    {
        IpFilter f;
        f.setAutoBlacklistThreshold(3);

        const std::string ip1 = "1.1.1.1";
        const std::string ip2 = "2.2.2.2";

        f.recordRequest(ip1);
        f.recordRequest(ip1);
        f.recordRequest(ip2);

        REQUIRE(f.isAllowed(ip1)); // only 2 requests — under threshold
        REQUIRE(f.isAllowed(ip2)); // only 1 request

        f.recordRequest(ip1); // 3rd → blacklisted
        REQUIRE(!f.isAllowed(ip1));
        REQUIRE(f.isAllowed(ip2));
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("auto-blacklist: expiry resets block");
    {
        IpFilter f;
        f.setAutoBlacklistThreshold(3);
        f.setAutoBlacklistWindow(std::chrono::seconds{60});
        // Keep this short so the test stays fast but deterministic.
        f.setAutoBlacklistDuration(kFastBlacklistDuration);

        const std::string ip = "10.20.30.40";
        f.recordRequest(ip);
        f.recordRequest(ip);
        f.recordRequest(ip); // threshold reached
        REQUIRE(!f.isAllowed(ip));

        // Wait past expiry with a small scheduling buffer.
        printf("  [sleeping %lldms for blacklist expiry...]\n",
            static_cast<long long>(kFastSleepBuffer.count()));
        std::this_thread::sleep_for(kFastSleepBuffer);

        REQUIRE(f.isAllowed(ip)); // should have expired
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("auto-blacklist: new window starts after window elapses");
    {
        IpFilter f;
        f.setAutoBlacklistThreshold(10);
        // Keep this short so the test stays fast but deterministic.
        f.setAutoBlacklistWindow(kFastRateWindow);
        f.setAutoBlacklistDuration(std::chrono::seconds{60});

        const std::string ip = "5.6.7.8";
        // 9 requests — just under threshold
        for (int i = 0; i < 9; ++i) f.recordRequest(ip);
        REQUIRE(f.isAllowed(ip));

        // Wait past the rate window with a small scheduling buffer.
        printf("  [sleeping %lldms for rate window reset...]\n",
            static_cast<long long>(kFastSleepBuffer.count()));
        std::this_thread::sleep_for(kFastSleepBuffer);

        // First request in new window: counter resets, still allowed
        f.recordRequest(ip);
        REQUIRE(f.isAllowed(ip));
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("static rule + auto-blacklist: static wins");
    {
        IpFilter f;
        writeConfig("Require not ip 203.0.113.0/24\n");
        f.loadConfig(kCfgPath);

        const std::string ip = "203.0.113.42";
        REQUIRE(!f.isAllowed(ip)); // blocked by static rule

        // Recording requests should not change anything for a statically
        // blocked IP
        f.recordRequest(ip);
        REQUIRE(!f.isAllowed(ip));
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("non-IPv4 address (e.g. empty) does not crash and is allowed");
    {
        IpFilter f;
        REQUIRE(f.isAllowed(""));
        REQUIRE(f.isAllowed("::1")); // IPv6 not parsed → allowed
        REQUIRE(f.isAllowed("not-an-ip"));
    }

    removeConfig();

    return test_summary();
}
