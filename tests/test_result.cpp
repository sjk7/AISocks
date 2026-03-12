// This is an independent project of an individual developer. Dear PVS-Studio,
// //-V002 please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Comprehensive tests for Result<T> - exception-free error handling
// Tests success/error state, copy/move semantics, lazy message construction

//-V002 Line numbers may be incorrect due to template expansion

#include "Result.h"
#include "TcpSocket.h"
#include "UdpSocket.h"
#include "SocketFactory.h"
#include "SocketTypes.h"
#include "test_helpers.h"
#include <string>
#include <vector>

using namespace aiSocks;

// Test helper: non-trivial type with move/copy tracking
struct TrackedObject { //-V690 //-V690
    int value;
    bool* moveFlag;
    bool* copyFlag;
    bool* destructorFlag;

    TrackedObject(
        int v, bool* mf = nullptr, bool* cf = nullptr, bool* df = nullptr)
        : value(v), moveFlag(mf), copyFlag(cf), destructorFlag(df) {}

    TrackedObject(const TrackedObject& other)
        : value(other.value)
        , moveFlag(other.moveFlag)
        , copyFlag(other.copyFlag)
        , destructorFlag(other.destructorFlag) {
        if (copyFlag) *copyFlag = true;
    }

    TrackedObject(TrackedObject&& other) noexcept
        : value(other.value)
        , moveFlag(other.moveFlag)
        , copyFlag(other.copyFlag)
        , destructorFlag(other.destructorFlag) {
        if (moveFlag) *moveFlag = true;
        other.value = -1; // Mark as moved-from
    }

    ~TrackedObject() {
        if (destructorFlag) *destructorFlag = true;
    }

    TrackedObject& operator=(const TrackedObject& other) {
        if (this != &other) {
            value = other.value;
            moveFlag = other.moveFlag;
            copyFlag = other.copyFlag;
            destructorFlag = other.destructorFlag;
            if (copyFlag) *copyFlag = true;
        }
        return *this;
    }

    TrackedObject& operator=(TrackedObject&& other) noexcept {
        if (this != &other) {
            value = other.value;
            moveFlag = other.moveFlag;
            copyFlag = other.copyFlag;
            destructorFlag = other.destructorFlag;
            if (moveFlag) *moveFlag = true;
            other.value = -1; // Mark as moved-from
        }
        return *this;
    }
};

int main() {
    printf("=== Result<T> Tests ===\n");

    // Test 1: Success case - construct with value
    BEGIN_TEST("Success: construct Result with value");
    {
        Result<int> result(42);
        REQUIRE(result.isSuccess());
        REQUIRE(!result.isError());
        REQUIRE(result.value() == 42);
    }

    // Test 2: Error case - construct with error
    BEGIN_TEST("Error: construct Result with error");
    {
        Result<int> result(
            SocketError::ConnectFailed, "Connection failed", 111);
        REQUIRE(!result.isSuccess());
        REQUIRE(result.isError());
        REQUIRE(result.error() == SocketError::ConnectFailed);
    }

    // Test 3: Error message construction
    BEGIN_TEST("Error: message() constructs error message lazily");
    {
        Result<int> result(SocketError::Timeout, "Operation timed out", 0);
        std::string msg = result.message(); //-V820
        REQUIRE(!msg.empty());
        REQUIRE(msg.find("timed out") != std::string::npos
            || msg.find("Timeout") != std::string::npos);
    }

    // Test 4: Copy constructor - success case
    BEGIN_TEST("Copy: success Result copies value");
    {
        Result<int> result1(123);
        Result<int> result2(result1);

        REQUIRE(result2.isSuccess());
        REQUIRE(result2.value() == 123);
        REQUIRE(result1.value() == 123); // Original unchanged
    }

    // Test 5: Copy constructor - error case
    BEGIN_TEST("Copy: error Result copies error info");
    {
        Result<int> result1(
            SocketError::SetOptionFailed, "Invalid argument", 22);
        Result<int> result2(result1);

        REQUIRE(result2.isError());
        REQUIRE(result2.error() == SocketError::SetOptionFailed);
    }

    // Test 6: Move constructor - success case
    BEGIN_TEST("Move: success Result moves value");
    {
        bool moved = false;
        bool destructed = false;

        Result<TrackedObject> result1(
            TrackedObject(789, &moved, nullptr, &destructed));
        REQUIRE(result1.isSuccess());
        //-V820
        Result<TrackedObject> result2(std::move(result1));
        REQUIRE(result2.isSuccess());
        REQUIRE(result2.value().value == 789);
    }

    // Test 7: Move constructor - error case
    BEGIN_TEST("Move: error Result moves error info");
    {
        Result<int> result1(SocketError::BindFailed, "Bind failed", 13);
        Result<int> result2(std::move(result1));
        //-V820
        REQUIRE(result2.isError());
        REQUIRE(result2.error() == SocketError::BindFailed);
    }

    // Test 8: Copy assignment - success to success
    BEGIN_TEST("Copy assignment: success to success");
    {
        Result<int> result1(100);
        Result<int> result2(200);

        result2 = result1; //-V820
        REQUIRE(result2.isSuccess());
        REQUIRE(result2.value() == 100);
    }

    // Test 9: Copy assignment - error to success
    BEGIN_TEST("Copy assignment: error to success");
    {
        Result<int> result1(SocketError::ListenFailed, "Listen failed");
        Result<int> result2(42);

        result2 = result1;
        REQUIRE(result2.isError());
        REQUIRE(result2.error() == SocketError::ListenFailed);
    }

    // Test 10: Copy assignment - success to error
    BEGIN_TEST("Copy assignment: success to error");
    {
        Result<int> result1(999);
        Result<int> result2(SocketError::AcceptFailed, "Accept failed");

        result2 = result1;
        REQUIRE(result2.isSuccess());
        REQUIRE(result2.value() == 999);
    }

    // Test 11: Move assignment - success to success
    BEGIN_TEST("Move assignment: success to success");
    {
        Result<int> result1(555);
        Result<int> result2(666);

        result2 = std::move(result1);
        REQUIRE(result2.isSuccess());
        REQUIRE(result2.value() == 555);
    }

    // Test 12: Self-assignment - copy (suppressed warnings)
    BEGIN_TEST("Self-assignment: copy assignment");
    {
        Result<int> result(777);
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif
        result = result; // Self-assign //-V570
#ifdef __clang__
#pragma clang diagnostic pop
#endif

        REQUIRE(result.isSuccess());
        REQUIRE(result.value() == 777);
    }

    // Test 13: Self-assignment - move (suppressed warnings)
    BEGIN_TEST("Self-assignment: move assignment");
    {
        Result<int> result(888);
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif
        result = std::move(result); // Self-move //-V570
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

        REQUIRE(result.isSuccess());
        // Value may be in moved-from state, but should not crash
    }

    // Test 14: value_or - success case
    BEGIN_TEST("value_or: returns value for success");
    {
        Result<int> result(42);
        int val = result.value_or(999);
        REQUIRE(val == 42);
    }

    // Test 15: value_or - error case
    BEGIN_TEST("value_or: returns fallback for error");
    {
        Result<int> result(SocketError::SendFailed, "Send failed");
        int val = result.value_or(999);
        REQUIRE(val == 999);
    }

    // Test 16: operator bool - success
    BEGIN_TEST("operator bool: returns true for success");
    {
        Result<int> result(1);
        REQUIRE(result);
        REQUIRE(static_cast<bool>(result) == true);
    }

    // Test 17: operator bool - error
    BEGIN_TEST("operator bool: returns false for error");
    {
        Result<int> result(SocketError::Unknown, "Unknown error");
        REQUIRE(!result);
        REQUIRE(static_cast<bool>(result) == false);
    }

    // Test 18: Lazy message construction
    BEGIN_TEST("Lazy message: message built only when accessed");
    {
        Result<int> result(SocketError::WouldBlock, "Would block", 4);
        // Message should not be built yet (lazy)
        // First access builds it
        std::string msg1 = result.message();
        REQUIRE(!msg1.empty()); //-V807

        // Second access reuses cached message
        std::string msg2 = result.message();
        REQUIRE(msg1 == msg2);
    }

    // Test 19: Non-trivial type - std::string
    BEGIN_TEST("Non-trivial type: Result<std::string>");
    {
        Result<std::string> result(std::string("Hello, World!"));
        REQUIRE(result.isSuccess());
        REQUIRE(result.value() == "Hello, World!");
    }

    // Test 20: Non-trivial type - std::vector
    BEGIN_TEST("Non-trivial type: Result<std::vector<int>>");
    {
        std::vector<int> vec = {1, 2, 3, 4, 5};
        Result<std::vector<int>> result(std::move(vec));

        REQUIRE(result.isSuccess());
        REQUIRE(result.value().size() == 5);
        REQUIRE(result.value()[0] == 1);
        REQUIRE(result.value()[4] == 5);
    }

    // Test 21: Socket type - Result<TcpSocket>
    BEGIN_TEST("Socket type: Result<TcpSocket> from SocketFactory");
    {
        auto result = SocketFactory::createTcpSocket();
        REQUIRE(result.isSuccess());
        REQUIRE(result.value().isValid());
    }

    // Test 22: Error propagation pattern
    BEGIN_TEST("Error propagation: chaining Result operations");
    {
        // Try to create a server on an invalid port (port 0 then try to connect
        // to it will fail) Or just verify that we can propagate errors through
        // Result
        auto socketResult = SocketFactory::createTcpSocket();
        REQUIRE(socketResult.isSuccess());

        // If we move the socket out, the Result now contains it
        TcpSocket sock = std::move(socketResult.value());
        REQUIRE(sock.isValid());

        // This test just verifies Result can hold socket types and propagate
        // through APIs Testing actual network errors would be unreliable across
        // //-V820 platforms
        REQUIRE(true);
    }

    // Test 23: Placement new correctness - destructor called
    BEGIN_TEST("Placement new: destructor called on union member");
    {
        bool destructed = false;
        {
            Result<TrackedObject> result(
                TrackedObject(1, nullptr, nullptr, &destructed)); //-V820
            REQUIRE(result.isSuccess());
        } // Result destructor should call TrackedObject destructor

        REQUIRE(destructed == true);
    }

    // Test 24: Mixed operations - error then success
    BEGIN_TEST("Mixed operations: error assigned to success");
    {
        Result<int> success(100);
        Result<int> error(SocketError::ConnectionReset, "Connection reset");

        success = error;
        REQUIRE(success.isError());
        REQUIRE(success.error() == SocketError::ConnectionReset);
    }

    // Test 25: Mixed operations - success then error
    BEGIN_TEST("Mixed operations: success assigned to error");
    {
        Result<int> error(SocketError::ConnectionReset, "Connection reset");
        Result<int> success(200);

        error = success;
        REQUIRE(error.isSuccess());
        REQUIRE(error.value() == 200);
    }

    // Test 26: DNS error handling
    BEGIN_TEST("DNS error: Result with DNS flag");
    {
        Result<int> result(
            SocketError::ConnectFailed, "Host not found", 0, true);
        REQUIRE(result.isError());
        REQUIRE(result.error() == SocketError::ConnectFailed);

        std::string msg = result.message();
        // Should mention DNS or host resolution
        REQUIRE(!msg.empty());
    }

    // Test 27: Error state preservation through copy
    BEGIN_TEST("Error state: preserved through copy");
    {
        Result<int> result1(SocketError::CreateFailed, "Create failed", 13);
        Result<int> result2(result1);

        REQUIRE(result2.error() == SocketError::CreateFailed);
        REQUIRE(result1.error() == SocketError::CreateFailed);
    }

    // Test 28: Error with zero sysCode
    BEGIN_TEST("Error: zero sysCode is valid");
    {
        Result<int> result(SocketError::Timeout, "Timeout", 0);
        REQUIRE(result.isError());
        // sysCode not exposed but should work internally
    }

    // Test 29: Success case - error() returns None
    BEGIN_TEST("Success: error() returns SocketError::None");
    {
        Result<int> result(42);
        REQUIRE(result.error() == SocketError::None);
    }

    // Test 30: Large object - move efficiency
    BEGIN_TEST("Large object: move avoids copy");
    {
        std::vector<int> largeVec(10000, 42);
        Result<std::vector<int>> result(std::move(largeVec));

        REQUIRE(result.isSuccess());
        REQUIRE(result.value().size() == 10000);
        // Move should have been used (no copy)
    }

    // Test 31: Const correctness - const value()
    BEGIN_TEST("Const correctness: const value() access");
    {
        const Result<int> result(42);
        REQUIRE(result.isSuccess());
        REQUIRE(result.value() == 42);
    }

    // Test 32: Const correctness - const message()
    BEGIN_TEST("Const correctness: const message() access");
    {
        const Result<int> result(SocketError::Unknown, "Unknown error");
        std::string msg = result.message();
        REQUIRE(!msg.empty());
    }

    // Test 33: Multiple error types
    BEGIN_TEST("Multiple error types: all SocketError values");
    {
        Result<int> r1(SocketError::ConnectFailed, "Connect failed");
        Result<int> r2(SocketError::Timeout, "Timeout");
        Result<int> r3(SocketError::SendFailed, "Send failed");

        REQUIRE(r1.error() != r2.error());
        REQUIRE(r2.error() != r3.error());
        REQUIRE(r1.error() != r3.error());
    }

    // Test 34: Empty/default state not allowed
    BEGIN_TEST("No default constructor: Result must be success or error");
    {
        // Result<int> r; // Should not compile - no default constructor
        // This test verifies the design choice at compile time
        REQUIRE(true); // Compilation success means test passes
    }

    // Test 35: Error info completeness
    BEGIN_TEST("Error info: all fields accessible");
    {
        Result<int> result(
            SocketError::BindFailed, "Address already in use", 98);

        REQUIRE(result.error() == SocketError::BindFailed);

        std::string msg = result.message();
        REQUIRE(!msg.empty());
        // Message should incorporate description
        REQUIRE(msg.find("Address") != std::string::npos
            || msg.find("address") != std::string::npos
            || msg.find("use") != std::string::npos
            || msg.find("Bind") != std::string::npos);
    }

    // Test 36: value() rvalue-ref overload
    BEGIN_TEST("value() &&: rvalue overload moves value out");
    {
        Result<std::string> result(std::string("rvalue-test"));
        std::string moved = std::move(result).value();
        REQUIRE(moved == "rvalue-test");
    }

    // Test 37: message() on a success Result returns empty string
    BEGIN_TEST("message() on success: returns empty string");
    {
        Result<int> result(42);
        const std::string& msg = result.message();
        REQUIRE(msg.empty());
    }

    // Test 38: move assignment error -> value (has_value_ false in destructor
    // branch)
    BEGIN_TEST(
        "Move assignment: error replaces success (has_value_ false path)");
    {
        Result<int> dst(100); // starts as success
        Result<int> src(SocketError::SendFailed, "send failed", 32);
        dst = std::move(src);
        REQUIRE(dst.isError());
        REQUIRE(dst.error() == SocketError::SendFailed);
    }

    // Test 39: buildErrorMessage with null description
    BEGIN_TEST("buildErrorMessage: null description returns \"Unknown error\"");
    {
        Result<int> result(SocketError::Unknown, nullptr, 0);
        const std::string& msg = result.message();
        REQUIRE(msg == "Unknown error");
    }

    // Test 40: buildErrorMessage with isDns=true (gai_strerror path)
    BEGIN_TEST("buildErrorMessage: isDns=true uses gai_strerror");
    {
        // EAI_NONAME (-2 on most POSIX systems) is a well-known DNS error code.
        Result<int> result(
            SocketError::ConnectFailed, "DNS lookup failed", -2, true);
        const std::string& msg = result.message();
        REQUIRE(!msg.empty());
        REQUIRE(msg.find("DNS lookup failed") != std::string::npos);
    }

    // Test 41: Result<bool> error instantiation (covers unexecuted
    // isError/error)
    BEGIN_TEST(
        "Result<bool>: error instantiation exercises bool specialisation");
    {
        Result<bool> r(SocketError::ConnectFailed, "bool error");
        REQUIRE(r.isError());
        REQUIRE(!r.isSuccess());
        REQUIRE(r.error() == SocketError::ConnectFailed);
    }

    // Test 42: Result<bool> success — exercises value_ref() for bool
    BEGIN_TEST("Result<bool>: success value access exercises value_ref");
    {
        Result<bool> r(true);
        REQUIRE(r.isSuccess());
        REQUIRE(r.value() == true);
    }

    // Test 43: Result<Port> error instantiation
    BEGIN_TEST(
        "Result<Port>: error instantiation exercises Port specialisation");
    {
        Result<Port> r(SocketError::BindFailed, "port error");
        REQUIRE(r.isError());
        REQUIRE(r.error() == SocketError::BindFailed);
    }

    // Test 44: Result<Port> success — exercises value_ref() for Port
    BEGIN_TEST("Result<Port>: success value access exercises value_ref");
    {
        Result<Port> r(Port{8080});
        REQUIRE(r.isSuccess());
        REQUIRE(r.value() == Port{8080});
    }

    // Test 45: Result<UdpSocket> move-constructor exercises UdpSocket
    // instantiation
    BEGIN_TEST("Result<UdpSocket>: move-ctor from error exercises UdpSocket "
               "specialisation");
    {
        Result<UdpSocket> r(SocketError::BindFailed, "udp error");
        Result<UdpSocket> r2(std::move(r));
        REQUIRE(r2.isError());
        REQUIRE(r2.error() == SocketError::BindFailed);
    }

    // Test 46: static factory - failure() for easy types
    BEGIN_TEST("static failure(): bool, Port, Endpoint factories");
    {
        auto rb = Result<bool>::failure(SocketError::ConnectFailed, "b-fail");
        REQUIRE(rb.isError());
        REQUIRE(rb.error() == SocketError::ConnectFailed);

        auto rp = Result<Port>::failure(SocketError::BindFailed, "p-fail");
        REQUIRE(rp.isError());
        REQUIRE(rp.error() == SocketError::BindFailed);

        auto re
            = Result<Endpoint>::failure(SocketError::ConnectFailed, "e-fail");
        REQUIRE(re.isError());
        REQUIRE(!static_cast<bool>(re)); // operator bool on error
    }

    // Test 47: static failure() for socket types
    BEGIN_TEST("static failure(): TcpSocket, UdpSocket, UnixSocket factories");
    {
        auto rt = Result<TcpSocket>::failure(
            SocketError::ConnectFailed, "tcp-fail");
        REQUIRE(rt.isError());
        REQUIRE(rt.error() == SocketError::ConnectFailed);
        REQUIRE(!rt.message().empty());

        auto ru
            = Result<UdpSocket>::failure(SocketError::BindFailed, "udp-fail");
        REQUIRE(ru.isError());

        auto rx = Result<UnixSocket>::failure(
            SocketError::ConnectFailed, "unix-fail");
        REQUIRE(rx.isError());
    }

    // Test 48: static success() for bool and Port
    BEGIN_TEST("static success(): bool and Port");
    {
        auto rb = Result<bool>::success(true);
        REQUIRE(rb.isSuccess());
        REQUIRE(rb.value() == true);

        auto rp = Result<Port>::success(Port{9090});
        REQUIRE(rp.isSuccess());
        REQUIRE(rp.value() == Port{9090});
    }

    // Test 49: static success() for Endpoint — exercises value_ref()
    BEGIN_TEST("static success(): Endpoint - value_ref() and operator bool");
    {
        Endpoint ep{"127.0.0.1", Port{80}, AddressFamily::IPv4};
        auto r = Result<Endpoint>::success(ep);
        REQUIRE(r.isSuccess());
        REQUIRE(static_cast<bool>(r));
        const Endpoint& ref = r.value();
        REQUIRE(ref.port == Port{80});
    }

    // Test 50: static success() for UdpSocket — exercises UdpSocket value_ref
    BEGIN_TEST(
        "static success(): UdpSocket - success factory with real socket");
    {
        UdpSocket udp{};
        auto r = Result<UdpSocket>::success(std::move(udp));
        REQUIRE(r.isSuccess());
        (void)r.value(); // exercises value_ref() for UdpSocket
    }

    // Test 51: Result<TcpSocket> error — isError, error(), message()
    BEGIN_TEST("Result<TcpSocket>: isError/error/message on failure result");
    {
        Result<TcpSocket> r(
            SocketError::ConnectFailed, "connection refused", 111);
        REQUIRE(r.isError());
        REQUIRE(r.error() == SocketError::ConnectFailed);
        const std::string& msg = r.message();
        REQUIRE(!msg.empty());
    }

    // -------------------------------------------------------------------
    // Result<bool>::message() — unexecuted instantiation coverage
    // -------------------------------------------------------------------

    // Test 52: Result<bool> message() on error exercises the bool instantiation
    BEGIN_TEST("Result<bool>: message() on error result");
    {
        Result<bool> r(SocketError::ConnectFailed, "bool msg test", 7);
        const std::string& msg = r.message();
        REQUIRE(!msg.empty());
        REQUIRE(msg.find("bool msg test") != std::string::npos);
    }

    // -------------------------------------------------------------------
    // Result<void> — completely untested (lines 378-445 all zero)
    // -------------------------------------------------------------------

    // Test 53: Result<void> success path
    BEGIN_TEST("Result<void>: success() factory and isSuccess/isError/operator "
               "bool");
    {
        Result<void> r = Result<void>::success();
        REQUIRE(r.isSuccess());
        REQUIRE(!r.isError());
        REQUIRE(static_cast<bool>(r));
        REQUIRE(r.error() == SocketError::None);
        const std::string& msg = r.message();
        REQUIRE(msg.empty()); // success → empty message
    }

    // Test 54: Result<void> error path via constructor
    BEGIN_TEST("Result<void>: error constructor and message() lazy build");
    {
        Result<void> r(SocketError::BindFailed, "void error", 98);
        REQUIRE(r.isError());
        REQUIRE(!r.isSuccess());
        REQUIRE(!static_cast<bool>(r));
        REQUIRE(r.error() == SocketError::BindFailed);
        const std::string& msg = r.message();
        REQUIRE(!msg.empty());
        REQUIRE(msg.find("void error") != std::string::npos);
    }

    // Test 55: Result<void> failure() and failureOwned() static factories
    BEGIN_TEST("Result<void>: failure() and failureOwned() static factories");
    {
        auto rf = Result<void>::failure(SocketError::SendFailed, "send void");
        REQUIRE(rf.isError());
        REQUIRE(rf.error() == SocketError::SendFailed);

        auto ro = Result<void>::failureOwned(
            SocketError::SendFailed, std::string("owned void"));
        REQUIRE(ro.isError());
        REQUIRE(ro.message().find("owned void") != std::string::npos);
    }

    // Test 56: Result<void> copy constructor and copy assignment
    BEGIN_TEST("Result<void>: copy ctor and copy assignment");
    {
        Result<void> src(SocketError::Timeout, "timeout void", 110);

        // copy ctor
        Result<void> copy(src);
        REQUIRE(copy.isError());
        REQUIRE(copy.error() == SocketError::Timeout);

        // copy assignment
        Result<void> dst = Result<void>::success();
        dst = src;
        REQUIRE(dst.isError());
        REQUIRE(dst.error() == SocketError::Timeout);

        // self-assign
        dst = *&dst;
        REQUIRE(dst.isError());
    }

    return test_summary();
}
