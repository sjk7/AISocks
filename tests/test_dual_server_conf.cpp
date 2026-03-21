// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// test_dual_server_conf.cpp
// Unit tests for the server_conf.h config file parser used by
// dual_http_https_server.

#include "server_conf.h"
#include "test_helpers.h"
#include <cstdio>
#include <fstream>
#include <string>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Write a string to a temp file and return the path.
static std::string writeTmp(const char* name, const char* content) {
    std::ofstream f(name);
    f << content;
    return name;
}

// Guard that removes a file on destruction.
struct RemoveOnExit {
    const char* path;
    ~RemoveOnExit() { std::remove(path); }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

int main() {
    printf("=== dual_server_conf (server_conf.h) Tests ===\n");

    // ------------------------------------------------------------------
    BEGIN_TEST("parseBool: truthy values");
    {
        REQUIRE(serverConfParseBool("1"));
        REQUIRE(serverConfParseBool("true"));
        REQUIRE(serverConfParseBool("yes"));
        REQUIRE(serverConfParseBool("on"));
    }

    BEGIN_TEST("parseBool: falsy values");
    {
        REQUIRE(!serverConfParseBool("0"));
        REQUIRE(!serverConfParseBool("false"));
        REQUIRE(!serverConfParseBool("no"));
        REQUIRE(!serverConfParseBool("off"));
        REQUIRE(!serverConfParseBool(""));
        // Case-sensitive: "True" / "TRUE" are NOT truthy.
        REQUIRE(!serverConfParseBool("True"));
        REQUIRE(!serverConfParseBool("TRUE"));
        REQUIRE(!serverConfParseBool("YES"));
    }

    BEGIN_TEST("trim: strips spaces and tabs");
    {
        REQUIRE(serverConfTrim("  hello  ") == "hello");
        REQUIRE(serverConfTrim("\t value \t") == "value");
        REQUIRE(serverConfTrim("no_spaces") == "no_spaces");
        REQUIRE(serverConfTrim("   ") == "");
        REQUIRE(serverConfTrim("") == "");
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("loadServerConf: missing file returns false");
    {
        ServerConf conf;
        REQUIRE(!loadServerConf("__no_such_file_xyz__.conf", conf));
        // conf must be unchanged — still defaults.
        REQUIRE(conf.httpPort == 8080);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("loadServerConf: parses all keys");
    {
        const char* path = "test_sc_all_keys.conf";
        RemoveOnExit guard{path};
        writeTmp(path,
            "www_root          = /srv/www\n"
            "http_port         = 9090\n"
            "https_port        = 9443\n"
            "cert              = /etc/ssl/my.crt\n"
            "key               = /etc/ssl/my.key\n"
            "enable_http       = false\n"
            "enable_https      = true\n"
            "index_file        = home.html\n"
            "directory_listing = on\n");

        ServerConf conf;
        REQUIRE(loadServerConf(path, conf));
        REQUIRE(conf.wwwRoot == "/srv/www");
        REQUIRE(conf.httpPort == 9090);
        REQUIRE(conf.httpsPort == 9443);
        REQUIRE(conf.cert == "/etc/ssl/my.crt");
        REQUIRE(conf.key == "/etc/ssl/my.key");
        REQUIRE(conf.enableHttp == false);
        REQUIRE(conf.enableHttps == true);
        REQUIRE(conf.indexFile == "home.html");
        REQUIRE(conf.directoryListing == true);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("loadServerConf: inline comments are stripped");
    {
        const char* path = "test_sc_comments.conf";
        RemoveOnExit guard{path};
        writeTmp(path,
            "http_port = 7777  # was 8080\n"
            "# full-line comment\n"
            "https_port = 7443\n");

        ServerConf conf;
        REQUIRE(loadServerConf(path, conf));
        REQUIRE(conf.httpPort == 7777);
        REQUIRE(conf.httpsPort == 7443);
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("loadServerConf: blank lines and whitespace-only lines ignored");
    {
        const char* path = "test_sc_blanks.conf";
        RemoveOnExit guard{path};
        writeTmp(path,
            "\n"
            "   \n"
            "http_port = 1234\n"
            "\n");

        ServerConf conf;
        REQUIRE(loadServerConf(path, conf));
        REQUIRE(conf.httpPort == 1234);
        // Everything else stays at default.
        REQUIRE(conf.wwwRoot == "./www");
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("loadServerConf: partial file leaves other fields at default");
    {
        const char* path = "test_sc_partial.conf";
        RemoveOnExit guard{path};
        writeTmp(path, "http_port = 5555\n");

        ServerConf conf;
        REQUIRE(loadServerConf(path, conf));
        REQUIRE(conf.httpPort == 5555);
        REQUIRE(conf.httpsPort == 8443); // unchanged default
        REQUIRE(conf.enableHttp == true); // unchanged default
    }

    // ------------------------------------------------------------------
    BEGIN_TEST("loadServerConf: enable_http / enable_https bool variants");
    {
        const char* path = "test_sc_bools.conf";
        RemoveOnExit guard{path};

        // Test each truthy token for enable_http.
        for (const char* tok : {"1", "true", "yes", "on"}) {
            ServerConf conf;
            conf.enableHttp = false; // start false
            {
                std::ofstream f(path);
                f << "enable_http = " << tok << "\n";
            }
            REQUIRE(loadServerConf(path, conf));
            REQUIRE_MSG(conf.enableHttp,
                (std::string("enable_http should be true for token: ") + tok)
                    .c_str());
        }

        // Test a falsy token.
        {
            ServerConf conf;
            conf.enableHttp = true;
            std::ofstream f(path);
            f << "enable_http = false\n";
            f.close();
            REQUIRE(loadServerConf(path, conf));
            REQUIRE(!conf.enableHttp);
        }
        std::remove(path);
    }

    // ------------------------------------------------------------------
    // CLI-wins-over-conf-file precedence (simulated in code — no fork needed)
    BEGIN_TEST("Precedence: CLI values override loaded config");
    {
        const char* path = "test_sc_precedence.conf";
        RemoveOnExit guard{path};
        writeTmp(path,
            "www_root   = /from/config\n"
            "http_port  = 9999\n"
            "https_port = 9998\n");

        ServerConf conf;
        REQUIRE(loadServerConf(path, conf));

        // Simulate the positional arg override in main():
        conf.wwwRoot = "/from/cli";
        conf.httpPort = static_cast<uint16_t>(8080);
        // https_port not supplied — stays at conf file value.

        REQUIRE(conf.wwwRoot == "/from/cli");
        REQUIRE(conf.httpPort == 8080);
        REQUIRE(conf.httpsPort == 9998); // conf file value survives
    }

    printf("\ndual_server_conf tests complete.\n");
    return 0;
}
