// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

// server_conf.h
// Shared config-file parser used by dual_http_https_server and its tests.
// Intentionally header-only so no extra compilation unit is needed.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

// ---------------------------------------------------------------------------
// ServerConf — holds all tunable server settings with built-in defaults.
// ---------------------------------------------------------------------------
struct ServerConf {
    std::string wwwRoot = "./www";
    uint16_t httpPort = 8080;
    uint16_t httpsPort = 8443;
    std::string cert = "server-cert.pem";
    std::string key = "server-key.pem";
    bool enableHttp = true;
    bool enableHttps = true;
    std::string indexFile = "index.html";
    bool directoryListing = true;
};

// ---------------------------------------------------------------------------
// Internal helpers (inline so they can appear in multiple TUs without ODR)
// ---------------------------------------------------------------------------
inline std::string serverConfTrim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Returns true for "1", "true", "yes", "on" (case-sensitive).
// Everything else is false — including "True", "TRUE", "FALSE", etc.
inline bool serverConfParseBool(const std::string& v) {
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

// ---------------------------------------------------------------------------
// loadServerConf
//
// Parse a key = value config file into conf.  Lines starting with '#' (after
// stripping whitespace) are comments; inline comments are also supported.
//
// Returns true if the file was opened successfully, false otherwise.
// On failure, conf is left unchanged.
// Unknown keys produce a warning to stderr.
// ---------------------------------------------------------------------------
inline bool loadServerConf(const std::string& path, ServerConf& conf) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        // Strip inline comments.
        const auto commentPos = line.find('#');
        if (commentPos != std::string::npos) line.erase(commentPos);

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const auto k = serverConfTrim(line.substr(0, eq));
        const auto v = serverConfTrim(line.substr(eq + 1));
        if (k.empty() || v.empty()) continue;

        if (k == "www_root")
            conf.wwwRoot = v;
        else if (k == "http_port")
            conf.httpPort = static_cast<uint16_t>(std::stoi(v));
        else if (k == "https_port")
            conf.httpsPort = static_cast<uint16_t>(std::stoi(v));
        else if (k == "cert")
            conf.cert = v;
        else if (k == "key")
            conf.key = v;
        else if (k == "enable_http")
            conf.enableHttp = serverConfParseBool(v);
        else if (k == "enable_https")
            conf.enableHttps = serverConfParseBool(v);
        else if (k == "index_file")
            conf.indexFile = v;
        else if (k == "directory_listing")
            conf.directoryListing = serverConfParseBool(v);
        else
            fprintf(stderr, "Warning: unknown config key '%s'\n", k.c_str());
    }
    return true;
}
