// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Tests for SocketTypes.h: Milliseconds, Port, Backlog, Endpoint,
// UnixPath, ServerBind, ConnectArgs, Timeouts namespace.

#include "SocketTypes.h"
#include "test_helpers.h"
#include <string>

using namespace aiSocks;

int main() {

    // -----------------------------------------------------------------------
    // Milliseconds
    // -----------------------------------------------------------------------

    BEGIN_TEST("Milliseconds: default construction is zero");
    {
        Milliseconds m;
        REQUIRE(m.count == 0);
        REQUIRE(m.milliseconds() == 0);
    }

    BEGIN_TEST("Milliseconds: explicit construction");
    {
        Milliseconds m{500};
        REQUIRE(m.count == 500);
        REQUIRE(m.milliseconds() == 500);
    }

    BEGIN_TEST("Milliseconds: operator== and operator!=");
    {
        REQUIRE(Milliseconds{100} == Milliseconds{100});
        REQUIRE(!(Milliseconds{100} == Milliseconds{200}));
        REQUIRE(Milliseconds{100} != Milliseconds{200});
        REQUIRE(!(Milliseconds{100} != Milliseconds{100}));
    }

    BEGIN_TEST("Milliseconds: operator< and operator<=");
    {
        REQUIRE(Milliseconds{100} < Milliseconds{200});
        REQUIRE(!(Milliseconds{200} < Milliseconds{100}));
        REQUIRE(Milliseconds{100} <= Milliseconds{100});
        REQUIRE(Milliseconds{100} <= Milliseconds{200});
        REQUIRE(!(Milliseconds{200} <= Milliseconds{100}));
    }

    BEGIN_TEST("Milliseconds: operator> and operator>=");
    {
        REQUIRE(Milliseconds{200} > Milliseconds{100});
        REQUIRE(!(Milliseconds{100} > Milliseconds{200}));
        REQUIRE(Milliseconds{200} >= Milliseconds{200});
        REQUIRE(Milliseconds{200} >= Milliseconds{100});
        REQUIRE(!(Milliseconds{100} >= Milliseconds{200}));
    }

    BEGIN_TEST("Milliseconds: arithmetic operators + - * /");
    {
        REQUIRE((Milliseconds{300} + Milliseconds{200}) == Milliseconds{500});
        REQUIRE((Milliseconds{500} - Milliseconds{200}) == Milliseconds{300});
        REQUIRE((Milliseconds{100} * int64_t{3}) == Milliseconds{300});
        REQUIRE((int64_t{4} * Milliseconds{100}) == Milliseconds{400});
        REQUIRE((Milliseconds{600} / int64_t{3}) == Milliseconds{200});
    }

    BEGIN_TEST("Milliseconds: named global constants");
    {
        REQUIRE(defaultTimeout == Milliseconds{30000});
        REQUIRE(defaultConnectTimeout == Milliseconds{10000});
        REQUIRE(poll_min == Milliseconds{-1});
        REQUIRE(wait_forever == Milliseconds{0});
    }

    BEGIN_TEST("Timeouts namespace constants");
    {
        REQUIRE(Timeouts::Immediate == poll_min);
        REQUIRE(Timeouts::Short == Milliseconds{1000});
        REQUIRE(Timeouts::Medium == Milliseconds{5000});
        REQUIRE(Timeouts::Long == Milliseconds{30000});
    }

    // -----------------------------------------------------------------------
    // Port
    // -----------------------------------------------------------------------

    BEGIN_TEST("Port: default construction is zero");
    {
        Port p;
        REQUIRE(p.value() == 0);
        REQUIRE(static_cast<uint16_t>(p) == 0);
    }

    BEGIN_TEST("Port: explicit construction and accessors");
    {
        Port p{8080};
        REQUIRE(p.value() == 8080);
        REQUIRE(static_cast<uint16_t>(p) == 8080);
    }

    BEGIN_TEST("Port: operator== and operator!=");
    {
        REQUIRE(Port{80} == Port{80});
        REQUIRE(!(Port{80} == Port{443}));
        REQUIRE(Port{80} != Port{443});
        REQUIRE(!(Port{80} != Port{80}));
    }

    BEGIN_TEST("Port: operator< and operator>");
    {
        REQUIRE(Port{80} < Port{443});
        REQUIRE(!(Port{443} < Port{80}));
        REQUIRE(Port{443} > Port{80});
        REQUIRE(!(Port{80} > Port{443}));
    }

    BEGIN_TEST("Port: named well-known port constants");
    {
        REQUIRE(Port::ftp == 21);
        REQUIRE(Port::ftpData == 20);
        REQUIRE(Port::ssh == 22);
        REQUIRE(Port::telnet == 23);
        REQUIRE(Port::smtp == 25);
        REQUIRE(Port::dns == 53);
        REQUIRE(Port::http == 80);
        REQUIRE(Port::pop3 == 110);
        REQUIRE(Port::imap == 143);
        REQUIRE(Port::https == 443);
        REQUIRE(Port::smtps == 465);
        REQUIRE(Port::smtpSubmit == 587);
        REQUIRE(Port::imaps == 993);
        REQUIRE(Port::pop3s == 995);
        REQUIRE(Port::httpAlt == 8080);
        REQUIRE(Port::httpsAlt == 8443);
        REQUIRE(Port::ephemeralStart == 49152);
        REQUIRE(Port::any.value() == 0);
    }

    // -----------------------------------------------------------------------
    // Backlog
    // -----------------------------------------------------------------------

    BEGIN_TEST("Backlog: default construction uses defaultBacklog");
    {
        Backlog b;
        REQUIRE(b.value == Backlog::defaultBacklog);
        REQUIRE(int(b) == Backlog::defaultBacklog);
    }

    BEGIN_TEST("Backlog: explicit construction and operator int");
    {
        Backlog b{256};
        REQUIRE(int(b) == 256);
    }

    BEGIN_TEST("Backlog: named platform constants");
    {
        REQUIRE(Backlog::somaxconnMacOS == 128);
        REQUIRE(Backlog::somaxconnLinux == 128);
        REQUIRE(Backlog::defaultBacklog == 64);
        REQUIRE(Backlog::maxBacklog > 0);
        // Windows sentinel value check
        REQUIRE(Backlog::somaxconnWindows == static_cast<int>(0x7fff'ffff));
    }

    // -----------------------------------------------------------------------
    // UnixPath (guarded by AISOCKS_HAVE_UNIX_SOCKETS)
    // -----------------------------------------------------------------------

#ifdef AISOCKS_HAVE_UNIX_SOCKETS
    BEGIN_TEST("UnixPath: construction and value()");
    {
        UnixPath p{"/tmp/test.sock"};
        REQUIRE(p.value() == "/tmp/test.sock");
    }

    BEGIN_TEST("UnixPath: operator== and operator!=");
    {
        UnixPath a{"/tmp/a.sock"};
        UnixPath b{"/tmp/b.sock"};
        UnixPath a2{"/tmp/a.sock"};
        REQUIRE(a == a2);
        REQUIRE(!(a == b));
        REQUIRE(a != b);
        REQUIRE(!(a != a2));
    }
#endif

    // -----------------------------------------------------------------------
    // Endpoint
    // -----------------------------------------------------------------------

    BEGIN_TEST("Endpoint: default construction");
    {
        Endpoint ep;
        REQUIRE(ep.address.empty());
        REQUIRE(ep.port == Port{});
    }

    BEGIN_TEST("Endpoint: value construction");
    {
        Endpoint ep{"192.168.1.1", Port{80}, AddressFamily::IPv4};
        REQUIRE(ep.address == "192.168.1.1");
        REQUIRE(ep.port == Port{80});
        REQUIRE(ep.family == AddressFamily::IPv4);
    }

    BEGIN_TEST("Endpoint: operator== and operator!=");
    {
        Endpoint a{"10.0.0.1", Port{8080}, AddressFamily::IPv4};
        Endpoint b{"10.0.0.1", Port{8080}, AddressFamily::IPv4};
        Endpoint c{"10.0.0.2", Port{8080}, AddressFamily::IPv4};
        REQUIRE(a == b);
        REQUIRE(!(a == c));
        REQUIRE(a != c);
        REQUIRE(!(a != b));
    }

    BEGIN_TEST("Endpoint: toString()");
    {
        Endpoint ep{"127.0.0.1", Port{9000}, AddressFamily::IPv4};
        REQUIRE(ep.toString() == "127.0.0.1:9000");
    }

    // -----------------------------------------------------------------------
    // ServerBind
    // -----------------------------------------------------------------------

    BEGIN_TEST("ServerBind: default construction");
    {
        ServerBind sb;
        REQUIRE(sb.address.empty());
        REQUIRE(sb.port == Port{});
        REQUIRE(sb.reuseAddr == true);
        REQUIRE(int(sb.backlog) == Backlog::defaultBacklog);
    }

    BEGIN_TEST("ServerBind: aggregate initialisation");
    {
        ServerBind sb{"0.0.0.0", Port{8080}, Backlog{128}, false};
        REQUIRE(sb.address == "0.0.0.0");
        REQUIRE(sb.port == Port{8080});
        REQUIRE(int(sb.backlog) == 128);
        REQUIRE(sb.reuseAddr == false);
    }

    // -----------------------------------------------------------------------
    // ConnectArgs
    // -----------------------------------------------------------------------

    BEGIN_TEST("ConnectArgs: default timeout is defaultConnectTimeout");
    {
        ConnectArgs ca{"localhost", Port{80}};
        REQUIRE(ca.address == "localhost");
        REQUIRE(ca.port == Port{80});
        REQUIRE(ca.connectTimeout == defaultConnectTimeout);
    }

    BEGIN_TEST("ConnectArgs: explicit timeout");
    {
        ConnectArgs ca{"example.com", Port{443}, Milliseconds{5000}};
        REQUIRE(ca.address == "example.com");
        REQUIRE(ca.port == Port{443});
        REQUIRE(ca.connectTimeout == Milliseconds{5000});
    }

    return test_summary();
}
