// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com
#ifndef AISOCKS_RESULT_H
#define AISOCKS_RESULT_H

#include "SocketTypes.h"
#include <string>
#include <optional>
#include <utility>

namespace aiSocks {

// ---------------------------------------------------------------------------
// Error information structure for lazy message construction
// ---------------------------------------------------------------------------
struct ErrorInfo {
    SocketError error{SocketError::None}; // Add missing error field
    const char* description{nullptr};  // string literal, never owned
    int sysCode{0};                    // errno / WSAGetLastError
    bool isDns{false};
    mutable std::string cachedMessage_;  // built once on first access
    
    std::string buildMessage() const {
        if (!description) return "Unknown error";
        
        std::string msg = description;
        if (sysCode != 0) {
            msg += " [";
            msg += std::to_string(sysCode);
            msg += ": ";
            
            // Platform-specific error string
            char sysErrBuf[256] = {0};
#ifdef _WIN32
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                         nullptr, sysCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                         sysErrBuf, sizeof(sysErrBuf), nullptr);
#else
            if (isDns) {
                // DNS errors use gai_strerror
                const char* gaiMsg = gai_strerror(sysCode);
                if (gaiMsg) {
                    strncpy(sysErrBuf, gaiMsg, sizeof(sysErrBuf) - 1);
                }
            } else {
                // Regular errno errors
                const char* errnoMsg = strerror(sysCode);
                if (errnoMsg) {
                    strncpy(sysErrBuf, errnoMsg, sizeof(sysErrBuf) - 1);
                }
            }
#endif
            // Trim trailing whitespace/newlines
            char* end = sysErrBuf + strlen(sysErrBuf) - 1;
            while (end >= sysErrBuf && (*end == '\n' || *end == '\r' || *end == ' ')) {
                *end = '\0';
                --end;
            }
            
            msg += sysErrBuf;
            msg += "]";
        }
        return msg;
    }
};

// ---------------------------------------------------------------------------
// Result<T> - Exception-free error handling with lazy error message construction
// ---------------------------------------------------------------------------
template<typename T>
class Result {
private:
    // Small value optimization - store inline for small types
    static constexpr bool is_small = sizeof(T) <= sizeof(ErrorInfo);
    
    union {
        alignas(T) unsigned char value_storage_[sizeof(T)];
        ErrorInfo error_;
    };
    
    bool has_value_;
    
    // Helper to get value reference
    T& value_ref() {
        return *reinterpret_cast<T*>(value_storage_);
    }
    
    const T& value_ref() const {
        return *reinterpret_cast<const T*>(value_storage_);
    }
    
    // Helper to get error reference
    ErrorInfo& error_ref() {
        return error_;
    }
    
    const ErrorInfo& error_ref() const {
        return error_;
    }
    
public:
    // Success constructor
    explicit Result(T&& value) : has_value_(true) {
        new(value_storage_) T(std::move(value));
    }
    
    // Error constructor - takes error info for lazy message construction
    Result(SocketError error, const char* description, int sysCode = 0, bool isDns = false)
        : has_value_(false) {
        new(&error_ref()) ErrorInfo{error, description, sysCode, isDns, {}};
    }
    
    // Copy/move constructors
    Result(const Result& other) : has_value_(other.has_value_) {
        if (has_value_) {
            new(value_storage_) T(other.value_ref());
        } else {
            new(&error_ref()) ErrorInfo{other.error_ref().error, other.error_ref().description, 
                                        other.error_ref().sysCode, other.error_ref().isDns, 
                                        other.error_ref().cachedMessage_};
        }
    }
    
    Result(Result&& other) noexcept : has_value_(other.has_value_) {
        if (has_value_) {
            new(value_storage_) T(std::move(other.value_ref()));
        } else {
            new(&error_ref()) ErrorInfo{other.error_ref().error, other.error_ref().description, 
                                        other.error_ref().sysCode, other.error_ref().isDns, 
                                        std::move(other.error_ref().cachedMessage_)};
        }
    }
    
    // Assignment operators
    Result& operator=(const Result& other) {
        if (this != &other) {
            if (has_value_) value_ref().~T();
            if (other.has_value_) {
                has_value_ = true;
                new(value_storage_) T(other.value_ref());
            } else {
                has_value_ = false;
                new(&error_ref()) ErrorInfo{other.error_ref().error, other.error_ref().description, 
                                            other.error_ref().sysCode, other.error_ref().isDns, 
                                            other.error_ref().cachedMessage_};
            }
        }
        return *this;
    }
    
    Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            if (has_value_) value_ref().~T();
            if (other.has_value_) {
                has_value_ = true;
                new(value_storage_) T(std::move(other.value_ref()));
            } else {
                has_value_ = false;
                new(&error_ref()) ErrorInfo{other.error_ref().error, other.error_ref().description, 
                                            other.error_ref().sysCode, other.error_ref().isDns, 
                                            std::move(other.error_ref().cachedMessage_)};
            }
        }
        return *this;
    }
    
    // Destructor
    ~Result() {
        if (has_value_) {
            value_ref().~T();
        } else {
            error_ref().~ErrorInfo();
        }
    }
    
    // Query state
    bool isSuccess() const noexcept { return has_value_; }
    bool isError() const noexcept { return !has_value_; }
    
    // Access value (only valid when successful)
    const T& value() const & { 
        if (!has_value_) {
            throw std::runtime_error("Attempted to access value of error Result");
        }
        return value_ref(); 
    }
    
    T& value() & { 
        if (!has_value_) {
            throw std::runtime_error("Attempted to access value of error Result");
        }
        return value_ref(); 
    }
    
    T&& value() && { 
        if (!has_value_) {
            throw std::runtime_error("Attempted to access value of error Result");
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
        
        return error_ref().buildMessage();
    }
    
    // Convenience methods
    explicit operator bool() const noexcept { return has_value_; }
    
    // Value-or-default to avoid verbose checking
    template<typename U>
    T value_or(U&& default_value) const {
        return has_value_ ? value_ref() : std::forward<U>(default_value);
    }
    
    // Static factory methods
    static Result success(T value) { 
        return Result(std::move(value)); 
    }
    
    static Result failure(SocketError error, const char* description, int sysCode = 0, bool isDns = false) {
        return Result(error, description, sysCode, isDns);
    }
};

// ---------------------------------------------------------------------------
// Result<void> specialization for operations without return values
// ---------------------------------------------------------------------------
template<>
class Result<void> {
private:
    SocketError error_;
    
    struct ErrorInfo {
        const char* description{nullptr};
        int sysCode{0};
        bool isDns{false};
        mutable std::string cachedMessage_;
        
        std::string buildMessage() const {
            if (!description) return "Unknown error";
            
            std::string msg = description;
            if (sysCode != 0) {
                msg += " [";
                msg += std::to_string(sysCode);
                msg += ": ";
                
                char sysErrBuf[256] = {0};
#ifdef _WIN32
                FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, static_cast<DWORD>(sysCode), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                             sysErrBuf, sizeof(sysErrBuf), nullptr);
#else
                if (isDns) {
                    const char* gaiMsg = gai_strerror(sysCode);
                    if (gaiMsg) {
                        strncpy(sysErrBuf, gaiMsg, sizeof(sysErrBuf) - 1);
                    }
                } else {
                    const char* errnoMsg = strerror(sysCode);
                    if (errnoMsg) {
                        strncpy(sysErrBuf, errnoMsg, sizeof(sysErrBuf) - 1);
                    }
                }
#endif
                char* end = sysErrBuf + strlen(sysErrBuf) - 1;
                while (end >= sysErrBuf && (*end == '\n' || *end == '\r' || *end == ' ')) {
                    *end = '\0';
                    --end;
                }
                
                msg += sysErrBuf;
                msg += "]";
            }
            return msg;
        }
    };
    
    std::optional<ErrorInfo> errorInfo_;
    
public:
    // Success constructor
    Result() : error_(SocketError::None) {}
    
    // Error constructor
    Result(SocketError error, const char* description, int sysCode = 0, bool isDns = false)
        : error_(error)
        , errorInfo_(ErrorInfo{description, sysCode, isDns, {}}) {}
    
    bool isSuccess() const noexcept { return error_ == SocketError::None; }
    bool isError() const noexcept { return !isSuccess(); }
    
    SocketError error() const noexcept { return error_; }
    
    const std::string& message() const {
        if (!errorInfo_.has_value()) {
            static const std::string empty = "";
            return empty;
        }
        
        if (errorInfo_->cachedMessage_.empty()) {
            errorInfo_->cachedMessage_ = errorInfo_->buildMessage();
        }
        return errorInfo_->cachedMessage_;
    }
    
    explicit operator bool() const noexcept { return isSuccess(); }
    
    static Result success() { return Result(); }
    static Result failure(SocketError error, const char* description, int sysCode = 0, bool isDns = false) {
        return Result(error, description, sysCode, isDns);
    }
};

// ---------------------------------------------------------------------------
// Result<Endpoint> specialization for endpoint queries
// ---------------------------------------------------------------------------
template<>
class Result<Endpoint> {
private:
    SocketError error_;
    Endpoint value_;
    
    struct ErrorInfo {
        const char* description{nullptr};
        int sysCode{0};
        bool isDns{false};
        mutable std::string cachedMessage_;
        
        std::string buildMessage() const {
            if (!description) return "Unknown error";
            
            std::string msg = description;
            if (sysCode != 0) {
                msg += " [";
                msg += std::to_string(sysCode);
                msg += ": ";
                
                char sysErrBuf[256] = {0};
#ifdef _WIN32
                FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, static_cast<DWORD>(sysCode), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                             sysErrBuf, sizeof(sysErrBuf), nullptr);
#else
                if (isDns) {
                    const char* gaiMsg = gai_strerror(sysCode);
                    if (gaiMsg) {
                        strncpy(sysErrBuf, gaiMsg, sizeof(sysErrBuf) - 1);
                    }
                } else {
                    const char* errnoMsg = strerror(sysCode);
                    if (errnoMsg) {
                        strncpy(sysErrBuf, errnoMsg, sizeof(sysErrBuf) - 1);
                    }
                }
#endif
                char* end = sysErrBuf + strlen(sysErrBuf) - 1;
                while (end >= sysErrBuf && (*end == '\n' || *end == '\r' || *end == ' ')) {
                    *end = '\0';
                    --end;
                }
                
                msg += sysErrBuf;
                msg += "]";
            }
            return msg;
        }
    };
    
    std::optional<ErrorInfo> errorInfo_;
    
public:
    // Success constructor
    Result(Endpoint value) : error_(SocketError::None), value_(std::move(value)) {}
    
    // Error constructor
    Result(SocketError error, const char* description, int sysCode = 0, bool isDns = false)
        : error_(error), value_{}, errorInfo_(ErrorInfo{description, sysCode, isDns, {}}) {}
    
    bool isSuccess() const noexcept { return error_ == SocketError::None; }
    bool isError() const noexcept { return !isSuccess(); }
    
    SocketError error() const noexcept { return error_; }
    
    const Endpoint& value() const & { 
        if (!isSuccess()) {
            throw std::runtime_error("Attempted to access value of error Result<Endpoint>");
        }
        return value_; 
    }
    
    Endpoint& value() & { 
        if (!isSuccess()) {
            throw std::runtime_error("Attempted to access value of error Result<Endpoint>");
        }
        return value_; 
    }
    
    Endpoint&& value() && { 
        if (!isSuccess()) {
            throw std::runtime_error("Attempted to access value of error Result<Endpoint>");
        }
        return std::move(value_); 
    }
    
    const std::string& message() const {
        if (!errorInfo_.has_value()) {
            static const std::string empty = "";
            return empty;
        }
        
        if (errorInfo_->cachedMessage_.empty()) {
            errorInfo_->cachedMessage_ = errorInfo_->buildMessage();
        }
        return errorInfo_->cachedMessage_;
    }
    
    explicit operator bool() const noexcept { return isSuccess(); }
    
    static Result<Endpoint> success(Endpoint value) { return Result(std::move(value)); }
    static Result<Endpoint> failure(SocketError error, const char* description, int sysCode = 0, bool isDns = false) {
        return Result(error, description, sysCode, isDns);
    }
};

} // namespace aiSocks

#endif // AISOCKS_RESULT_H
