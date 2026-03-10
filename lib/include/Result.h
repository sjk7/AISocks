// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifndef AISOCKS_RESULT_H
#define AISOCKS_RESULT_H

#include "SocketTypes.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace aiSocks {

// ---------------------------------------------------------------------------
// buildErrorMessage — compiler-firewall helper
//
// WHY a free function instead of a method?
//   Formatting an error message requires platform headers: <netdb.h> on POSIX
//   (for gai_strerror), <windows.h> / <winsock2.h> on Windows (for
//   FormatMessageA).  Including those in this header would drag them into
//   every translation unit that touches Result — inflating compile times and
//   polluting the preprocessor namespace with thousands of macros.
//
//   By declaring only the signature here and defining it in Result.cpp, only
//   that one file pays the include cost.  This is the classic "compiler
//   firewall" (aka "pImpl lite" for free functions).
//
// WHY called at most once per Result?
//   The caller (message()) caches the returned string in a unique_ptr<string>
//   and short-circuits on subsequent calls, so this function is only ever
//   entered once per Result instance regardless of how many times message()
//   is called.
// ---------------------------------------------------------------------------
std::string buildErrorMessage(const char* description, int sysCode, bool isDns);

// ---------------------------------------------------------------------------
// Result<T>
//
// Exception-free error handling for operations that return a value on success.
//
// DESIGN GOALS
//   1. Zero heap allocation on both success and error construction — errors
//      are reported very frequently (every WouldBlock on a non-blocking recv
//      counts), so constructing one must be as cheap as setting a few fields.
//   2. Lazy message string — the formatted "recv failed [11: Resource
//      temporarily unavailable]" string is almost never read; building it
//      eagerly on every WouldBlock would waste CPU and always allocate.
//      Instead it is built on first message() call only, and cached.
//   3. Platform headers stay behind the compiler firewall (see above).
//   4. Minimal placement-new boilerplate — old designs put both T and
//      ErrorInfo in a union, requiring placement-new and explicit destructor
//      calls for both arms.  Here, only T needs placement-new; the four error
//      fields are plain POD that live as ordinary members alongside
//      value_storage_ and need no special construction or destruction.
//
// STORAGE LAYOUT
//   value_storage_  raw aligned bytes for T; valid only when has_value_ == true
//   has_value_      the discriminator
//   error_          SocketError code; zero-cost SocketError::None on success
//   description_    pointer to a string literal (call-site constant, not owned)
//   sysCode_        errno / WSAGetLastError captured at error-construction time
//   isDns_          distinguishes gai_strerror from strerror in
//   buildErrorMessage cachedMessage_  null until the first message() call;
//   self-destructs
//
// WHY value_storage_ is not in a union with the error fields
//   Previous versions put T and ErrorInfo in a union so that in the error case
//   T's bytes were "reused" for ErrorInfo, keeping sizeof(Result<T>) minimal.
//   That forced placement-new AND explicit ~ErrorInfo() calls, which doubled
//   the copy/move/assign boilerplate.  The current layout accepts that the
//   error fields (one enum + one pointer + one int + one bool = ~16 bytes)
//   always occupy space alongside value_storage_.  For the value types used
//   here (Endpoint, int, ...) that is a negligible size increase and a large
//   readability win.
//
// SPECIAL-MEMBER RULES
//   Copy ctor/assign  copies T via placement-new, or copies the four POD
//                     fields.  cachedMessage_ is intentionally NOT copied —
//                     unique_ptr has no copy constructor anyway, and it is
//                     cheaper to rebuild lazily on the copy than to deep-copy
//                     a string that may never be read.
//   Move ctor/assign  moves T via placement-new, or moves the four POD fields.
//                     cachedMessage_ is moved via unique_ptr's cheap pointer
//                     swap — no string copy, no allocation.
//   Destructor        calls ~T() only on the success arm.  The error arm is
//                     plain POD (no destructor needed) and cachedMessage_
//                     (unique_ptr<string>) self-destructs as a normal member.
// ---------------------------------------------------------------------------
template <typename T> class [[nodiscard]] Result {
    private:
    // WHY placement-new instead of just "T value_"?
    //   T may not be default-constructible (e.g. Endpoint requires an address).
    //   Declaring "T value_" would force default-construction even on the error
    //   path.  Raw bytes defer construction until we know we have a value.
    alignas(T) unsigned char value_storage_[sizeof(T)];

    // WHY this field ordering?
    //   C++ lays out members in declaration order and inserts padding to
    //   satisfy alignment requirements.  Grouping same-sized fields together
    //   eliminates wasted padding bytes.  The sequence chosen here is:
    //
    //   1. value_storage_  — size/align determined by T (already above)
    //   2. error_          — 4-byte enum, packed directly after value_storage_
    //   3. sysCode_        — 4-byte int, no gap needed
    //   4. has_value_      — 1-byte bool  }  packed together into one
    //   5. isDns_          — 1-byte bool  }  4-byte slot (2 bytes padding
    //   after)
    //   6. description_    — 8-byte pointer, 2-byte pad fills out to
    //   8-alignment
    //   7. cachedMessage_  — 8-byte unique_ptr
    //
    //   For T = int (4 bytes) this gives sizeof(Result<int>) = 32 vs 40 with
    //   the naive ordering (has_value_ first forces a 3-byte pad before error_,
    //   and isDns_ forces a 3-byte pad before description_).

    // Error fields — always present, only meaningful when has_value_ == false.
    // Keeping them as plain data (not in a union arm) means no placement-new
    // and no explicit destructor call for the error arm.
    SocketError error_{SocketError::None};
    int sysCode_{0}; // errno / WSAGetLastError at error time
    bool has_value_{false};
    bool isDns_{false}; // true → gai_strerror, false → strerror
    // 2 bytes padding here (implicit, to align description_ to 8 bytes)
    const char* description_{
        nullptr}; // points to a string literal, never owned

    // WHY unique_ptr<string> instead of a plain string member?
    //   A default-constructed std::string still occupies ~32 bytes on the
    //   stack/inline, and SSO means very short strings avoid a heap alloc —
    //   but we cannot guarantee the message is short.  More importantly, on
    //   the hot WouldBlock path the message is NEVER read, so we want truly
    //   zero work at construction time.  unique_ptr starts as a null pointer
    //   (one machine word) and only allocates when message() is first called.
    mutable std::unique_ptr<std::string> cachedMessage_;

    T& value_ref() noexcept { return *reinterpret_cast<T*>(value_storage_); }
    const T& value_ref() const noexcept {
        return *reinterpret_cast<const T*>(value_storage_);
    }

    public:
    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    // Success path — placement-new constructs T directly in value_storage_.
    // No heap allocation; the error fields remain at their benign defaults.
    explicit Result(T&& value)
        : error_()
        , sysCode_(0)
        , has_value_(true)
        , isDns_(false)
        , description_(nullptr) {
        new (value_storage_) T(std::move(value));
    }

    // Error path — stores four POD fields, nothing else.  Zero allocation.
    // The description pointer must point to a string literal (or other storage
    // that outlives this Result), because we never copy the string itself.
    Result(SocketError error, const char* description, int sysCode = 0,
        bool isDns = false)
        : error_(error)
        , sysCode_(sysCode)
        , has_value_(false)
        , isDns_(isDns)
        , description_(description) {}

    // -----------------------------------------------------------------------
    // Copy
    //
    // WHY not copy cachedMessage_?
    //   unique_ptr is not copyable, so we physically cannot.  But even if we
    //   could (via make_unique<string>(*other.cachedMessage_)), it would be
    //   wasteful — in the common case the copy is never asked for its message,
    //   and we would have paid for an allocation and a string copy for nothing.
    //   Leaving cachedMessage_ null means it is built lazily on first access,
    //   exactly as it was on the original.
    // -----------------------------------------------------------------------
    Result(const Result& other)
        : error_(other.error_)
        , sysCode_(other.sysCode_)
        , has_value_(other.has_value_)
        , isDns_(other.isDns_)
        , description_(other.description_) {
        // cachedMessage_ intentionally left null — rebuilt lazily if needed
        if (has_value_) new (value_storage_) T(other.value_ref());
    }

    Result& operator=(const Result& other) {
        if (this != &other) {
            // WHY call ~T() explicitly?  value_storage_ is raw bytes — the
            // compiler does not know a T lives there, so it will not call ~T()
            // automatically.  We must do it ourselves before overwriting.
            if (has_value_) value_ref().~T();
            has_value_ = other.has_value_;
            error_ = other.error_;
            description_ = other.description_;
            sysCode_ = other.sysCode_;
            isDns_ = other.isDns_;
            cachedMessage_.reset(); // discard our cached string (see copy-ctor)
            if (has_value_) new (value_storage_) T(other.value_ref());
        }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Move
    //
    // WHY move cachedMessage_ instead of resetting it?
    //   If the source already built its message string, we should inherit it
    //   for free via pointer move rather than throw it away and re-build.
    //   unique_ptr::move is a single pointer swap — no allocation, no copy.
    // -----------------------------------------------------------------------
    Result(Result&& other) noexcept
        : error_(other.error_)
        , sysCode_(other.sysCode_)
        , has_value_(other.has_value_)
        , isDns_(other.isDns_)
        , description_(other.description_)
        , cachedMessage_(std::move(other.cachedMessage_)) {
        if (has_value_) new (value_storage_) T(std::move(other.value_ref()));
    }

    Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            if (has_value_) value_ref().~T(); // see copy-assign for why
            has_value_ = other.has_value_;
            error_ = other.error_;
            description_ = other.description_;
            sysCode_ = other.sysCode_;
            isDns_ = other.isDns_;
            cachedMessage_ = std::move(other.cachedMessage_);
            if (has_value_)
                new (value_storage_) T(std::move(other.value_ref()));
        }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Destruction
    //
    // WHY only call ~T() on the success arm?
    //   The error arm contains only POD fields (no destructor) and
    //   cachedMessage_ (a unique_ptr member that self-destructs normally).
    //   There is nothing to manually clean up on the error path.
    // -----------------------------------------------------------------------
    ~Result() {
        if (has_value_) value_ref().~T();
    }

    // -----------------------------------------------------------------------
    // Observers
    // -----------------------------------------------------------------------

    bool isSuccess() const noexcept { return has_value_; }
    bool isError() const noexcept { return !has_value_; }
    explicit operator bool() const noexcept { return has_value_; }

    SocketError error() const noexcept {
        return has_value_ ? SocketError::None : error_;
    }

    // Access value — Precondition: isSuccess() must be true.
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

    // Lazy error message.
    //
    // WHY the null-check on cachedMessage_ instead of an empty-string check?
    //   An empty string "" could theoretically be a valid (if unhelpful) built
    //   message.  A null pointer unambiguously means "not yet built".
    //
    // WHY return a reference to static empty on the success path?
    //   Returns a stable const std::string& so callers can hold a reference
    //   without the string going away.  Returning "" by value would not allow
    //   that pattern.  The static is initialised once and never changes.
    const std::string& message() const {
        static const std::string empty;
        if (has_value_) return empty;
        if (!cachedMessage_)
            cachedMessage_ = std::make_unique<std::string>(
                buildErrorMessage(description_, sysCode_, isDns_));
        return *cachedMessage_;
    }

    // Value-or-default — avoids verbose isSuccess() + value() at call sites.
    template <typename U> T value_or(U&& default_value) const {
        return has_value_ ? value_ref() : std::forward<U>(default_value);
    }

    // -----------------------------------------------------------------------
    // Static factory methods — preferred over the constructors at call sites
    // because the intent ("this is a success" / "this is a failure") is
    // self-documenting without needing to read the constructor signatures.
    // -----------------------------------------------------------------------
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(SocketError error, const char* description,
        int sysCode = 0, bool isDns = false) {
        return Result(error, description, sysCode, isDns);
    }
    // Cold-path only: use when the full message is already built as a string
    // (e.g. forwarding getErrorMessage()). Named distinctly so it cannot be
    // called accidentally in place of the zero-alloc const char* overload.
    static Result failureOwned(SocketError error, std::string message) {
        Result r(error, nullptr, 0, false);
        r.cachedMessage_ = std::make_unique<std::string>(std::move(message));
        return r;
    }
};

// ---------------------------------------------------------------------------
// Result<void>
//
// Specialisation for operations that succeed or fail but return no value
// (e.g. send, bind, listen).
//
// WHY a specialisation at all?
//   The generic Result<T> stores raw bytes for T and needs placement-new.
//   With T = void that is meaningless.  This specialisation has no
//   value_storage_ at all — it is just five plain fields — so there is no
//   union, no placement-new, and no manual destructor call anywhere.  The
//   compiler-generated destructor handles everything.
//
// WHY not inherit from a base or use a helper struct?
//   Either approach would add indirection and obscure the simple data layout.
//   The specialisation is small enough that the duplication is acceptable.
//
// The lazy-message semantics (unique_ptr<string>, buildErrorMessage) are
// identical to Result<T> — see the comments there for the reasoning.
// ---------------------------------------------------------------------------
template <> class [[nodiscard]] Result<void> {
    private:
    SocketError error_{SocketError::None};
    const char* description_{nullptr};
    int sysCode_{0};
    bool isDns_{false};
    bool has_error_{false};
    mutable std::unique_ptr<std::string> cachedMessage_;

    public:
    // Success — every field at its benign default; no work required.
    Result() = default;

    // Error — stores POD fields only.  Zero allocation.
    Result(SocketError error, const char* description, int sysCode = 0,
        bool isDns = false)
        : error_(error)
        , description_(description)
        , sysCode_(sysCode)
        , isDns_(isDns)
        , has_error_(true) {}

    // Copy — must be user-defined because unique_ptr<string> is not copyable.
    // cachedMessage_ is left null intentionally: rebuilt lazily on first access
    // (see the equivalent comment in Result<T>::copy constructor above).
    Result(const Result& other)
        : error_(other.error_)
        , description_(other.description_)
        , sysCode_(other.sysCode_)
        , isDns_(other.isDns_)
        , has_error_(other.has_error_) {}

    Result& operator=(const Result& other) {
        if (this != &other) {
            error_ = other.error_;
            description_ = other.description_;
            sysCode_ = other.sysCode_;
            isDns_ = other.isDns_;
            has_error_ = other.has_error_;
            cachedMessage_.reset(); // discard our cached string; rebuilt lazily
        }
        return *this;
    }

    // Move — defaulted because all members (POD + unique_ptr) are
    // individually movable and there is no raw storage needing special care.
    Result(Result&&) noexcept = default;
    Result& operator=(Result&&) noexcept = default;
    ~Result() = default;

    bool isSuccess() const noexcept { return !has_error_; }
    bool isError() const noexcept { return has_error_; }
    explicit operator bool() const noexcept { return isSuccess(); }

    SocketError error() const noexcept {
        return has_error_ ? error_ : SocketError::None;
    }

    const std::string& message() const {
        static const std::string empty;
        if (!has_error_) return empty;
        if (!cachedMessage_)
            cachedMessage_ = std::make_unique<std::string>(
                buildErrorMessage(description_, sysCode_, isDns_));
        return *cachedMessage_;
    }

    static Result success() { return Result(); }
    static Result failure(SocketError error, const char* description,
        int sysCode = 0, bool isDns = false) {
        return Result(error, description, sysCode, isDns);
    }
    // Cold-path only: use when the full message is already built as a string.
    // Named distinctly so it cannot be called accidentally in place of the
    // zero-alloc const char* overload.
    static Result failureOwned(SocketError error, std::string message) {
        Result r(error, nullptr, 0, false);
        r.cachedMessage_ = std::make_unique<std::string>(std::move(message));
        return r;
    }
};

} // namespace aiSocks

#endif // AISOCKS_RESULT_H
