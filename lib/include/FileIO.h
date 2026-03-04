// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace aiSocks {

/// File locking mode
enum class LockMode {
    None, // No locking
    Shared, // Shared lock (multiple readers allowed)
    Exclusive // Exclusive lock (single writer, no readers)
};

/// Simple C-style file I/O wrapper - no exceptions, no iostreams
/// Lightweight alternative to std::fstream for better performance and
/// simplicity
class File {
    public:
    File() = default;
    explicit File(const char* filename, const char* mode);
    ~File();

    // Non-copyable but movable
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;

    bool open(const char* filename, const char* mode);
    void close();
    void releaseLock();

    bool isOpen() const;
    bool eof() const;

    size_t read(void* buffer, size_t size, size_t count);
    size_t write(const void* buffer, size_t size, size_t count);
    bool writeString(const char* str);
    bool writeString(const std::string& str);
    bool printf(const char* format, ...);
    bool flush();
#ifdef _WIN32
    bool seek(long long offset, int whence);
    long long tell();
#else
    bool seek(int64_t offset, int whence);
    int64_t tell();
#endif
    size_t size();

    /// File information structure for descriptor-based checks
    struct FileInfo {
        bool valid = false;
        bool isSymlink = false;
        bool isRegular = false;
        bool isDirectory = false;
        size_t size = 0;
        time_t lastModified = 0;
    };

    /// Get file info from the already-open file descriptor (TOCTOU-safe)
    FileInfo getInfoFromDescriptor() const;

    std::vector<char> readAll();

    FILE* get() const;
    operator bool() const;

    private:
    FILE* file_ = nullptr;
    LockMode lockMode_ = LockMode::None;

    static LockMode determineLockMode(const char* mode);
    bool applyLock(LockMode mode);
};

/// Simple string builder using C-style allocation
class StringBuilder {
    public:
    StringBuilder() = default;
    explicit StringBuilder(size_t initialCapacity);
    ~StringBuilder();

    // Non-copyable but movable
    StringBuilder(const StringBuilder&) = delete;
    StringBuilder& operator=(const StringBuilder&) = delete;
    StringBuilder(StringBuilder&& other) noexcept;
    StringBuilder& operator=(StringBuilder&& other) noexcept;

    void reserve(size_t newCapacity);
    void append(const char* str);
    void append(const char* str, size_t len);
    void append(const std::string& str);
    void append(char c);

    // Template: must stay inline in header
    template <typename... Args>
    bool appendFormat(const char* format, Args... args) {
        if (!format) return false;

        char smallBuf[256];
        int needed = snprintf(smallBuf, sizeof(smallBuf), format, args...);

        if (needed < 0) return false;

        if (static_cast<size_t>(static_cast<unsigned int>(needed))
            < sizeof(smallBuf)) {
            append(smallBuf,
                static_cast<size_t>(static_cast<unsigned int>(needed)));
            return true;
        }

        size_t requiredSize
            = static_cast<size_t>(static_cast<unsigned int>(needed)) + 1;
        reserve(size_ + requiredSize);

        if (!buffer_) return false;

        int result = snprintf(buffer_ + size_, requiredSize, format, args...);
        if (result > 0) {
            size_ += static_cast<size_t>(static_cast<unsigned int>(result));
            return true;
        }

        return false;
    }

    void clear();
    const char* data() const;
    size_t size() const;
    bool empty() const;
    std::string toString() const;

    private:
    char* buffer_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
};

} // namespace aiSocks
