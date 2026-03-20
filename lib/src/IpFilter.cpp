// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "IpFilter.h"
#include "FileIO.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace aiSocks {

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

bool IpFilter::parseIpv4(const std::string& s, uint32_t& out) {
    const char* p = s.c_str();
    char* end = nullptr;
    unsigned long parts[4];
    for (int i = 0; i < 4; ++i) {
        parts[i] = std::strtoul(p, &end, 10);
        if (end == p || parts[i] > 255) return false;
        if (i < 3) {
            if (*end != '.') return false;
            p = end + 1;
        }
    }
    if (*end != '\0') return false;
    out = (static_cast<uint32_t>(parts[0]) << 24u)
        | (static_cast<uint32_t>(parts[1]) << 16u)
        | (static_cast<uint32_t>(parts[2]) << 8u)
        | static_cast<uint32_t>(parts[3]);
    return true;
}

// Parse one of:
//   "a.b.c.d"          exact host (/32)
//   "a.b.c.d/n"        CIDR
//   "a.b.*"            wildcard — converted to an 8/16/24-bit prefix
bool IpFilter::parseCidr(const std::string& rule, CidrRule& out) {
    // ── Wildcard ──────────────────────────────────────────────────────────
    const size_t starPos = rule.find('*');
    if (starPos != std::string::npos) {
        // Strip everything from the '*' onward (including any preceding '.')
        std::string prefix = rule.substr(0, starPos);
        if (!prefix.empty() && prefix.back() == '.') prefix.pop_back();

        uint32_t addr = 0;
        int octets = 0;
        size_t pos = 0;
        while (pos <= prefix.size()) {
            const size_t dot = prefix.find('.', pos);
            const std::string oct = prefix.substr(
                pos, dot == std::string::npos ? dot : dot - pos);
            const unsigned v = static_cast<unsigned>(std::atoi(oct.c_str()));
            if (v > 255) return false;
            addr = (addr << 8u) | v;
            ++octets;
            if (dot == std::string::npos) break;
            pos = dot + 1;
        }
        if (octets < 1 || octets > 3) return false;

        addr <<= static_cast<unsigned>(8 * (4 - octets));
        const unsigned prefixLen = static_cast<unsigned>(octets * 8);
        out.mask = (prefixLen == 0u) ? 0u : (0xFFFFFFFFu << (32u - prefixLen)); //-V547
        out.network = addr & out.mask;
        return true;
    }

    // ── CIDR ──────────────────────────────────────────────────────────────
    const size_t slash = rule.find('/');
    if (slash != std::string::npos) {
        const std::string ipPart = rule.substr(0, slash);
        const int prefixLen = std::atoi(rule.c_str() + slash + 1);
        if (prefixLen < 0 || prefixLen > 32) return false;
        uint32_t addr = 0;
        if (!parseIpv4(ipPart, addr)) return false;
        out.mask = (prefixLen == 0)
            ? 0u
            : (0xFFFFFFFFu << (32u - static_cast<unsigned>(prefixLen)));
        out.network = addr & out.mask;
        return true;
    }

    // ── Exact host (/32) ──────────────────────────────────────────────────
    uint32_t addr = 0;
    if (!parseIpv4(rule, addr)) return false;
    out.network = addr;
    out.mask = 0xFFFFFFFFu;
    return true;
}

bool IpFilter::isPrivate(uint32_t addr) noexcept {
    if ((addr & 0xFF000000u) == 0x0A000000u) return true; // 10.0.0.0/8
    if ((addr & 0xFFF00000u) == 0xAC100000u) return true; // 172.16.0.0/12
    if ((addr & 0xFFFF0000u) == 0xC0A80000u) return true; // 192.168.0.0/16
    return false;
}

bool IpFilter::matchesStaticRule(uint32_t addr) const noexcept {
    for (const auto& r : rules_)
        if ((addr & r.mask) == r.network) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

IpFilter::IpFilter(const std::string& configPath) {
    loadConfig(configPath);
}

// ---------------------------------------------------------------------------
// loadConfig
// ---------------------------------------------------------------------------

bool IpFilter::loadConfig(const std::string& path) {
    File f(path.c_str(), "r");
    if (!f.isOpen()) return false;

    std::vector<CidrRule> newRules;
    bool newLocalOnly = false;

    char lineBuf[512];
    while (
        fgets(lineBuf, static_cast<int>(sizeof(lineBuf)), f.get()) != nullptr) {
        std::string line(lineBuf);

        // Strip trailing CR/LF
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();

        // Strip inline comment
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);

        // Trim leading/trailing whitespace
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        const size_t last = line.find_last_not_of(" \t");
        line = line.substr(first, last - first + 1);
        if (line.empty()) continue;

        // Lowercase copy for keyword matching
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        // "Require localonly"
        if (lower == "require localonly") {
            newLocalOnly = true;
            continue;
        }

        // "Require not ip <token>"
        if (lower.substr(0, 14) == "require not ip") {
            const size_t ruleStart = line.find_last_of(" \t");
            if (ruleStart == std::string::npos) continue;
            const std::string ruleStr = line.substr(ruleStart + 1);
            if (ruleStr.empty()) continue;
            CidrRule cr;
            if (parseCidr(ruleStr, cr)) newRules.push_back(cr);
        }
    }

    rules_ = std::move(newRules);
    localOnly_ = newLocalOnly;
    return true;
}

// ---------------------------------------------------------------------------
// isAllowed
// ---------------------------------------------------------------------------

bool IpFilter::isAllowed(const std::string& peerAddress) {
    const auto now = std::chrono::steady_clock::now();

    // Auto-blacklist check (lazy expiry cleanup)
    auto blIt = autoBlacklist_.find(peerAddress);
    if (blIt != autoBlacklist_.end()) {
        if (now < blIt->second) return false; // still blacklisted
        autoBlacklist_.erase(blIt); // expired — remove
    }

    uint32_t addr = 0;
    if (!parseIpv4(peerAddress, addr)) return true; // non-IPv4 → allow

    // Static config rules
    if (matchesStaticRule(addr)) return false;

    // Local-only mode
    if (localOnly_ && !isLoopback(addr) && !isPrivate(addr)) return false;

    return true;
}

// ---------------------------------------------------------------------------
// recordRequest
// ---------------------------------------------------------------------------

void IpFilter::recordRequest(const std::string& peerAddress) {
    const auto now = std::chrono::steady_clock::now();

    auto& entry = rateTracker_[peerAddress];
    const auto elapsed = now - entry.windowStart;

    if (elapsed >= autoBlacklistWindow_) {
        // Start fresh window
        entry.count = 1;
        entry.windowStart = now;
    } else {
        ++entry.count;
        if (entry.count >= autoBlacklistThreshold_) {
            autoBlacklist_[peerAddress] = now + autoBlacklistDuration_;
            rateTracker_.erase(peerAddress);
        }
    }
}

// ---------------------------------------------------------------------------
// isAutoBlacklisted (inspection helper)
// ---------------------------------------------------------------------------

bool IpFilter::isAutoBlacklisted(const std::string& peerAddress) const {
    const auto now = std::chrono::steady_clock::now();
    const auto it = autoBlacklist_.find(peerAddress);
    if (it == autoBlacklist_.end()) return false;
    return now < it->second;
}

} // namespace aiSocks
