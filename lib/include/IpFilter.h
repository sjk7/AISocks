// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

// ---------------------------------------------------------------------------
// IpFilter — Apache-style IP blocking with auto-blacklist.
//
// Config file format (must NOT live in document root):
//   # Comment lines start with '#'
//   Require not ip 5.34.240.0/21       # CIDR block
//   Require not ip 92.40.215.169       # exact IP  (treated as /32)
//   Require not ip 92.40.*             # wildcard  (converted to CIDR prefix)
//   Require localonly                   # reject all non-loopback/RFC-1918 IPs
//
// Auto-blacklist:
//   IPs that exceed `autoBlacklistThreshold_` requests within
//   `autoBlacklistWindow_` are silently blocked for `autoBlacklistDuration_`.
//   Defaults: >200 req / 60 s → blocked for 90 minutes.
//
// Thread-safety: NOT thread-safe.  Must be called from the poll-loop thread.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace aiSocks {

class IpFilter {
    public:
    IpFilter() = default;
    explicit IpFilter(const std::string& configPath);

    // Load (or reload) rules from a config file.
    // Returns true on success; existing rules are unchanged on error.
    bool loadConfig(const std::string& path);

    // Returns true when peerAddress is permitted to connect/request.
    // Checks static config rules and the live auto-blacklist.
    // Does NOT count a new request — call recordRequest() for that.
    bool isAllowed(const std::string& peerAddress);

    // Record one request from peerAddress.
    // Updates rate-tracking and promotes to auto-blacklist on threshold breach.
    void recordRequest(const std::string& peerAddress);

    // ---- configuration knobs -------------------------------------------

    // Block every IP that is not loopback (127.x.x.x) or RFC-1918 private.
    void setLocalOnly(bool v) { localOnly_ = v; }

    // Maximum requests within autoBlacklistWindow_ before blacklisting.
    void setAutoBlacklistThreshold(int n) { autoBlacklistThreshold_ = n; }

    // Sliding window duration for the rate counter (default: 60 s).
    void setAutoBlacklistWindow(std::chrono::steady_clock::duration w) {
        autoBlacklistWindow_ = w;
    }

    // How long a rate-blacklisted IP stays blocked (default: 90 min).
    void setAutoBlacklistDuration(std::chrono::steady_clock::duration d) {
        autoBlacklistDuration_ = d;
    }

    // ---- inspection (primarily for tests) ------------------------------
    bool isAutoBlacklisted(const std::string& peerAddress) const;
    size_t autoBlacklistSize() const { return autoBlacklist_.size(); }
    size_t rateTrackerSize() const { return rateTracker_.size(); }
    size_t configRuleCount() const { return rules_.size(); }
    bool isLocalOnly() const { return localOnly_; }

    private:
    // A CIDR rule stored in host-byte-order for cheap masking.
    struct CidrRule {
        uint32_t network; // network address after masking
        uint32_t mask; // subnet mask (all-ones suffix = 0)
    };

    bool localOnly_{false};
    std::vector<CidrRule> rules_; // static deny rules from config file

    // Auto-blacklist: ip string → expiry (steady_clock)
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        autoBlacklist_;

    // Rate tracking: ip string → {count, window_start}
    struct RateEntry {
        int count{0};
        std::chrono::steady_clock::time_point windowStart{};
    };
    std::unordered_map<std::string, RateEntry> rateTracker_;

    int autoBlacklistThreshold_{200};
    std::chrono::steady_clock::duration autoBlacklistWindow_
        = std::chrono::seconds{60};
    std::chrono::steady_clock::duration autoBlacklistDuration_
        = std::chrono::seconds{5400}; // 90 min

    // Parse "a.b.c.d" into a host-byte-order uint32.  Returns false on error.
    static bool parseIpv4(const std::string& s, uint32_t& out);

    // Parse a rule token: "a.b.c.d", "a.b.c.d/n", or "a.b.*" wildcard.
    static bool parseCidr(const std::string& rule, CidrRule& out);

    static bool isLoopback(uint32_t addr) noexcept {
        return (addr >> 24) == 127u;
    }
    static bool isPrivate(uint32_t addr) noexcept;

    bool matchesStaticRule(uint32_t addr) const noexcept;
};

} // namespace aiSocks
