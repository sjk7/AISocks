// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_RESULT_H
#define AISOCKS_RESULT_H

#include "SocketTypes.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <utility>

namespace aiSocks {

// ---------------------------------------------------------------------------
// Error information structure for lazy message construction
// ---------------------------------------------------------------------------
struct ErrorInfo {
    SocketError error{SocketError::None};
    const char* description{nullptr}; // string literal, never owned
    int sysCode{0}; // errno / WSAGetLastError
    bool isDns{false};
    mutable std::string cachedMessage_; // built once on first access

    // Platform-agnostic message building - delegates to implementation
    std::string buildMessage() const;
};

// ---------------------------------------------------------------------------
// Result<T> - Exception-free error handling with lazy error message
// construction
// ---------------------------------------------------------------------------
template <typename T> class [[nodiscard]] Result {
    private:
    // Union storage - uses placement new to construct objects in raw memory
    // This enables zero-allocation Result with same size as T + bool
    union {
        alignas(T) unsigned char value_storage_[sizeof(
            T)]; // Raw bytes for T (placement new target)
        ErrorInfo error_; // ErrorInfo object (placement new target)
    };

    bool has_value_;

    // Helper to get value reference
    T& value_ref() { return *reinterpret_cast<T*>(value_storage_); }

    const T& value_ref() const {
        return *reinterpret_cast<const T*>(value_storage_);
    }

    // Helper to get error reference
    ErrorInfo& error_ref() { return error_; }

    const ErrorInfo& error_ref() const { return error_; }

    public:
    // Success constructor - placement new constructs T in raw storage
    // No heap allocation - constructs T directly in value_storage_ bytes
    explicit Result(T&& value) : has_value_(true) {
        new (value_storage_)
            T(std::move(value)); // <- Placement new: construct T in raw memory
    }

    // Error constructor - takes error info for lazy message construction
    Result(SocketError error, const char* description, int sysCode = 0,
        bool isDns = false)
        : has_value_(false) {
        new (&error_ref()) ErrorInfo{error, description, sysCode, isDns, {}};
    }

    // Copy constructor - placement new constructs objects in union storage
    // No heap allocation - constructs T or ErrorInfo directly in union memory
    Result(const Result& other) : has_value_(other.has_value_) {
        if (has_value_) {
            new (value_storage_)
                T(other.value_ref()); // <- Placement new: copy construct T in
                                      // raw memory
        } else {
            new (&error_ref()) ErrorInfo{other.error_ref().error,
                other.error_ref().description, other.error_ref().sysCode,
                other.error_ref().isDns,
                other.error_ref().cachedMessage_}; // <- Placement new: copy
                                                   // construct ErrorInfo
        }
    }

    Result(Result&& other) noexcept : has_value_(other.has_value_) {
        if (has_value_) {
            new (value_storage_) T(std::move(other.value_ref()));
        } else {
            new (&error_ref()) ErrorInfo{other.error_ref().error,
                other.error_ref().description, other.error_ref().sysCode,
                other.error_ref().isDns,
                std::move(other.error_ref().cachedMessage_)};
        }
    }

    // Assignment operator - manual destructor + placement new for object
    // lifetime management Critical: Must manually destroy old object before
    // placement new over same memory
    Result& operator=(const Result& other) {
        if (this != &other) {
            if (has_value_)
                value_ref().~T(); // <- Manual destructor call required!
            if (other.has_value_) {
                has_value_ = true;
                new (value_storage_)
                    T(other.value_ref()); // <- Placement new: copy construct T
            } else {
                has_value_ = false;
                new (&error_ref()) ErrorInfo{other.error_ref().error,
                    other.error_ref().description, other.error_ref().sysCode,
                    other.error_ref().isDns,
                    other.error_ref().cachedMessage_}; // <- Placement new: copy
                                                       // construct ErrorInfo
            }
        }
        return *this;
    }

    Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            if (has_value_) value_ref().~T();
            if (other.has_value_) {
                has_value_ = true;
                new (value_storage_) T(std::move(other.value_ref()));
            } else {
                has_value_ = false;
                new (&error_ref()) ErrorInfo{other.error_ref().error,
                    other.error_ref().description, other.error_ref().sysCode,
                    other.error_ref().isDns,
                    std::move(other.error_ref().cachedMessage_)};
            }
        }
        return *this;
    }

    // Destructor - manual object destruction required for union with placement
    // new Must explicitly destroy the object that was placement new constructed
    ~Result() {
        if (has_value_) {
            value_ref()
                .~T(); // <- Manual destructor call for placement new object
        } else {
            error_ref().~ErrorInfo(); // <- Manual destructor call for placement
                                      // new object
        }
    }

    // Query state
    bool isSuccess() const noexcept { return has_value_; }
    bool isError() const noexcept { return !has_value_; }

    // Access value — Precondition: isSuccess() must be true.
    // Use isSuccess(), operator bool, or value_or() to guard before calling.
    // Always prints to stderr on violation; asserts (aborts) in debug builds.
    const T& value() const& {
        if (!has_value_) {
            fprintf(stderr,
                "Result::value() called on an error Result"
                " — check isSuccess() first\n");
            assert(false);
        }
        return value_ref();
    }

    T& value() & {
        if (!has_value_) {
            fprintf(stderr,
                "Result::value() called on an error Result"
                " — check isSuccess() first\n");
            assert(false);
        }
        return value_ref();
    }

    T&& value() && {
        if (!has_value_) {
            fprintf(stderr,
                "Result::value() called on an error Result"
                " — check isSuccess() first\n");
            assert(false);
        }
        return std::move(value_ref());
    }

    // Access error information
    SocketError error() const noexcept {
        return has_value_ ? SocketError::None : error_ref().error;
    }

    // Lazy error message construction - built only once on first call
    // Uses thread-local buffer to avoid allocations
    const std::string& message() const {
        if (has_value_) {
            static const std::string empty = "";
            return empty;
        }

        // Build and cache the message
        if (error_ref().cachedMessage_.empty()) {
            error_ref().cachedMessage_ = error_ref().buildMessage();
        }
        return error_ref().cachedMessage_;
    }

    // Convenience methods
    explicit operator bool() const noexcept { return has_value_; }

    // Value-or-default to avoid verbose checking
    template <typename U> T value_or(U&& default_value) const {
        return has_value_ ? value_ref() : std::forward<U>(default_value);
    }

    // Static factory methods
    static Result success(T value) { return Result(std::move(value)); }

    static Result failure(SocketError error, const char* description,
        int sysCode = 0, bool isDns = false) {
        return Result(error, description, sysCode, isDns);
    }
};

// ---------------------------------------------------------------------------
// Result<void> specialization for operations without return values
// ---------------------------------------------------------------------------
template <> class [[nodiscard]] Result<void> {
    private:
    SocketError error_;

    struct ErrorInfo {
        const char* description{nullptr};
        int sysCode{0};
        bool isDns{false};
        mutable std::string cachedMessage_;

        // Platform-agnostic message building - delegates to implementation
        std::string buildMessage() const;
    };

    // Use union to avoid extra allocation when no error occurs
    union {
        ErrorInfo error_info_;
        char empty_; // Used when no error
    };

    bool has_error_;

    public:
    Result() : error_(SocketError::None), has_error_(false) {}

    Result(SocketError error, const char* description, int sysCode = 0,
        bool isDns = false)
        : error_(error), has_error_(true) {
        new (&error_info_) ErrorInfo{description, sysCode, isDns, {}};
    }

    ~Result() {
        if (has_error_) {
            error_info_.~ErrorInfo();
        }
    }

    // Copy/move constructors
    Result(const Result& other)
        : error_(other.error_), has_error_(other.has_error_) {
        if (has_error_) {
            new (&error_info_) ErrorInfo{other.error_info_.description,
                other.error_info_.sysCode, other.error_info_.isDns,
                other.error_info_.cachedMessage_};
        }
    }

    Result(Result&& other) noexcept
        : error_(other.error_), has_error_(other.has_error_) {
        if (has_error_) {
            new (&error_info_) ErrorInfo{other.error_info_.description,
                other.error_info_.sysCode, other.error_info_.isDns,
                std::move(other.error_info_.cachedMessage_)};
        }
    }

    // Assignment operators
    Result& operator=(const Result& other) {
        if (this != &other) {
            if (has_error_) error_info_.~ErrorInfo();
            if (other.has_error_) {
                has_error_ = true;
                new (&error_info_) ErrorInfo{other.error_info_.description,
                    other.error_info_.sysCode, other.error_info_.isDns,
                    other.error_info_.cachedMessage_};
            } else {
                has_error_ = false;
            }
        }
        return *this;
    }

    Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            if (has_error_) error_info_.~ErrorInfo();
            if (other.has_error_) {
                has_error_ = true;
                new (&error_info_) ErrorInfo{other.error_info_.description,
                    other.error_info_.sysCode, other.error_info_.isDns,
                    std::move(other.error_info_.cachedMessage_)};
            } else {
                has_error_ = false;
            }
        }
        return *this;
    }

    bool isSuccess() const noexcept { return !has_error_; }
    bool isError() const noexcept { return has_error_; }

    SocketError error() const noexcept { return error_; }

    const std::string& message() const {
        if (!has_error_) {
            static const std::string empty = "";
            return empty;
        }

        if (error_info_.cachedMessage_.empty()) {
            error_info_.cachedMessage_ = error_info_.buildMessage();
        }
        return error_info_.cachedMessage_;
    }

    explicit operator bool() const noexcept { return isSuccess(); }

    static Result success() { return Result(); }
    static Result failure(SocketError error, const char* description,
        int sysCode = 0, bool isDns = false) {
        return Result(error, description, sysCode, isDns);
    }
};

// ---------------------------------------------------------------------------
// Result<Endpoint> specialization for endpoint queries
// ---------------------------------------------------------------------------
template <> class [[nodiscard]] Result<Endpoint> {
    private:
    union {
        alignas(Endpoint) unsigned char endpoint_storage_[sizeof(Endpoint)];
        ErrorInfo error_;
    };

    bool has_value_;

    // Helper to get endpoint reference
    Endpoint& endpoint_ref() {
        return *reinterpret_cast<Endpoint*>(endpoint_storage_);
    }

    const Endpoint& endpoint_ref() const {
        return *reinterpret_cast<const Endpoint*>(endpoint_storage_);
    }

    // Helper to get error reference
    ErrorInfo& error_ref() { return error_; }

    const ErrorInfo& error_ref() const { return error_; }

    public:
    // Success constructor
    Result(Endpoint value) : has_value_(true) {
        new (endpoint_storage_) Endpoint(std::move(value));
    }

    // Error constructor
    Result(SocketError error, const char* description, int sysCode = 0,
        bool isDns = false)
        : has_value_(false) {
        new (&error_ref()) ErrorInfo{error, description, sysCode, isDns, {}};
    }

    // Copy/move constructors
    Result(const Result& other) : has_value_(other.has_value_) {
        if (has_value_) {
            new (endpoint_storage_) Endpoint(other.endpoint_ref());
        } else {
            new (&error_ref()) ErrorInfo{other.error_ref().error,
                other.error_ref().description, other.error_ref().sysCode,
                other.error_ref().isDns, other.error_ref().cachedMessage_};
        }
    }

    Result(Result&& other) noexcept : has_value_(other.has_value_) {
        if (has_value_) {
            new (endpoint_storage_) Endpoint(std::move(other.endpoint_ref()));
        } else {
            new (&error_ref()) ErrorInfo{other.error_ref().error,
                other.error_ref().description, other.error_ref().sysCode,
                other.error_ref().isDns,
                std::move(other.error_ref().cachedMessage_)};
        }
    }

    // Assignment operators
    Result& operator=(const Result& other) {
        if (this != &other) {
            if (has_value_) endpoint_ref().~Endpoint();
            if (other.has_value_) {
                has_value_ = true;
                new (endpoint_storage_) Endpoint(other.endpoint_ref());
            } else {
                has_value_ = false;
                new (&error_ref()) ErrorInfo{other.error_ref().error,
                    other.error_ref().description, other.error_ref().sysCode,
                    other.error_ref().isDns, other.error_ref().cachedMessage_};
            }
        }
        return *this;
    }

    Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            if (has_value_) endpoint_ref().~Endpoint();
            if (other.has_value_) {
                has_value_ = true;
                new (endpoint_storage_)
                    Endpoint(std::move(other.endpoint_ref()));
            } else {
                has_value_ = false;
                new (&error_ref()) ErrorInfo{other.error_ref().error,
                    other.error_ref().description, other.error_ref().sysCode,
                    other.error_ref().isDns,
                    std::move(other.error_ref().cachedMessage_)};
            }
        }
        return *this;
    }

    // Destructor
    ~Result() {
        if (has_value_) {
            endpoint_ref().~Endpoint();
        } else {
            error_ref().~ErrorInfo();
        }
    }

    bool isSuccess() const noexcept { return has_value_; }
    bool isError() const noexcept { return !has_value_; }

    SocketError error() const noexcept {
        return has_value_ ? SocketError::None : error_ref().error;
    }

    // Access value — Precondition: isSuccess() must be true.
    // Use isSuccess(), operator bool, or value_or() to guard before calling.
    // Always prints to stderr on violation; asserts (aborts) in debug builds.
    const Endpoint& value() const& {
        if (!has_value_) {
            fprintf(stderr,
                "Result<Endpoint>::value() called on an error Result"
                " — check isSuccess() first\n");
            assert(false);
        }
        return endpoint_ref();
    }

    Endpoint& value() & {
        if (!has_value_) {
            fprintf(stderr,
                "Result<Endpoint>::value() called on an error Result"
                " — check isSuccess() first\n");
            assert(false);
        }
        return endpoint_ref();
    }

    Endpoint&& value() && {
        if (!has_value_) {
            fprintf(stderr,
                "Result<Endpoint>::value() called on an error Result"
                " — check isSuccess() first\n");
            assert(false);
        }
        return std::move(endpoint_ref());
    }

    const std::string& message() const {
        if (has_value_) {
            static const std::string empty = "";
            return empty;
        }

        // Build and cache the message
        if (error_ref().cachedMessage_.empty()) {
            error_ref().cachedMessage_ = error_ref().buildMessage();
        }
        return error_ref().cachedMessage_;
    }

    explicit operator bool() const noexcept { return has_value_; }

    template <typename U> Endpoint value_or(U&& default_value) const {
        return has_value_ ? endpoint_ref() : std::forward<U>(default_value);
    }

    static Result success(Endpoint value) { return Result(std::move(value)); }
    static Result failure(SocketError error, const char* description,
        int sysCode = 0, bool isDns = false) {
        return Result(error, description, sysCode, isDns);
    }
};

} // namespace aiSocks

#endif // AISOCKS_RESULT_H
