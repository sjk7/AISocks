// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <memory>
#include <stdarg.h>
#include <errno.h>

#ifdef _WIN32
    #include <io.h>
    #include <sys/locking.h>
    #include <sys/stat.h>
    #include <windows.h>
    // Define S_ISDIR and S_ISREG if not already defined (for MSVC)
    #ifndef S_ISDIR
        #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
    #endif
    #ifndef S_ISREG
        #define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
    #endif
#else
    #include <unistd.h>
    #include <sys/file.h>
    #include <sys/stat.h>
#endif

namespace aiSocks {

/// File locking mode
enum class LockMode {
    None,      // No locking
    Shared,    // Shared lock (multiple readers allowed)
    Exclusive  // Exclusive lock (single writer, no readers)
};

/// Simple C-style file I/O wrapper - no exceptions, no iostreams
/// Lightweight alternative to std::fstream for better performance and simplicity
class File {
public:
    File() = default;
    
    explicit File(const char* filename, const char* mode) {
        open(filename, mode);
    }
    
    ~File() {
        close();
    }
    
    // Non-copyable but movable
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& other) noexcept : file_(other.file_) {
        other.file_ = nullptr;
    }
    File& operator=(File&& other) noexcept {
        if (this != &other) {
            close();
            file_ = other.file_;
            other.file_ = nullptr;
        }
        return *this;
    }
    
    bool open(const char* filename, const char* mode) {
        close();
        
        // Determine lock mode based on file mode
        LockMode lockMode = determineLockMode(mode);
        
#ifdef _WIN32
        errno_t err = fopen_s(&file_, filename, mode);
        if (err != 0 || !file_) return false;
        
        if (!applyLock(lockMode)) {
            fclose(file_);
            file_ = nullptr;
            return false;
        }
        return true;
#else
        file_ = fopen(filename, mode);
        if (!file_) return false;
        
        if (!applyLock(lockMode)) {
            fclose(file_);
            file_ = nullptr;
            return false;
        }
        return true;
#endif
    }
    
    void close() {
        if (file_) {
            releaseLock();
            fclose(file_);
            file_ = nullptr;
            lockMode_ = LockMode::None;
        }
    }
    
    /// Release file lock
    void releaseLock() {
        if (!file_ || lockMode_ == LockMode::None) return;
        
#ifdef _WIN32
        if (lockMode_ == LockMode::Exclusive) {
            int fd = _fileno(file_);
            if (fd != -1) {
                _locking(fd, _LK_UNLCK, 0x7FFFFFFF);
            }
        }
#else
        int fd = fileno(file_);
        if (fd != -1) {
            flock(fd, LOCK_UN);
        }
#endif
    }
    
    bool isOpen() const {
        return file_ != nullptr;
    }
    
    bool eof() const {
        return file_ ? feof(file_) != 0 : true;
    }
    
    size_t read(void* buffer, size_t size, size_t count) {
        return file_ ? fread(buffer, size, count, file_) : 0;
    }
    
    size_t write(const void* buffer, size_t size, size_t count) {
        return file_ ? fwrite(buffer, size, count, file_) : 0;
    }
    
    bool writeString(const char* str) {
        if (!file_ || !str) return false;
        size_t len = strlen(str);
        return write(str, 1, len) == len;
    }
    
    bool writeString(const std::string& str) {
        return write(str.data(), 1, str.size()) == str.size();
    }
    
    bool printf(const char* format, ...) {
        if (!file_) return false;
        
        va_list args;
        va_start(args, format);
        int result = vfprintf(file_, format, args);
        va_end(args);
        
        return result >= 0;
    }
    
    bool flush() {
        return file_ ? fflush(file_) == 0 : false;
    }
    
    bool seek(long offset, int whence) {
        return file_ ? fseek(file_, offset, whence) == 0 : false;
    }
    
    long tell() {
        return file_ ? ftell(file_) : -1;
    }
    
    size_t size() {
        if (!file_) return 0;
        
        long current = tell();
        if (current < 0) return 0;
        
        if (!seek(0, SEEK_END)) return 0;
        long fileSize = tell();
        
        seek(current, SEEK_SET);
        return fileSize > 0 ? static_cast<size_t>(fileSize) : 0;
    }
    
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
    /// This checks the actual file we have open, not the path
    FileInfo getInfoFromDescriptor() const {
        FileInfo info;
        if (!file_) return info;
        
#ifdef _WIN32
        int fd = _fileno(file_);
        if (fd == -1) return info;
        
        struct stat st;
        if (fstat(fd, &st) != 0) return info;
        
        info.valid = true;
        info.isDirectory = S_ISDIR(st.st_mode);
        info.isRegular = S_ISREG(st.st_mode);
        info.size = static_cast<size_t>(st.st_size);
        info.lastModified = st.st_mtime;
        
        // Check for reparse point (symlink) on Windows
        HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
        if (h != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION fileInfo;
            if (GetFileInformationByHandle(h, &fileInfo)) {
                info.isSymlink = (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            }
        }
#else
        int fd = fileno(file_);
        if (fd == -1) return info;
        
        struct stat st;
        if (fstat(fd, &st) != 0) return info;
        
        info.valid = true;
        info.isSymlink = S_ISLNK(st.st_mode);
        info.isDirectory = S_ISDIR(st.st_mode);
        info.isRegular = S_ISREG(st.st_mode);
        info.size = static_cast<size_t>(st.st_size);
        info.lastModified = st.st_mtime;
#endif
        
        return info;
    }
    
    std::vector<char> readAll() {
        std::vector<char> result;
        if (!file_) return result;
        
        size_t fileSize = size();
        if (fileSize == 0) return result;
        
        result.resize(fileSize);
        size_t bytesRead = read(result.data(), 1, fileSize);
        result.resize(bytesRead); // Resize in case of partial read
        
        return result;
    }
    
    FILE* get() const {
        return file_;
    }
    
    operator bool() const {
        return isOpen();
    }

private:
    FILE* file_ = nullptr;
    LockMode lockMode_ = LockMode::None;
    
    /// Determine lock mode from file open mode string
    static LockMode determineLockMode(const char* mode) {
        if (!mode) return LockMode::None;
        
        // Check for write modes: 'w', 'a', or '+' (read-write)
        bool hasWrite = (strchr(mode, 'w') != nullptr);
        bool hasAppend = (strchr(mode, 'a') != nullptr);
        bool hasPlus = (strchr(mode, '+') != nullptr);
        
        if (hasWrite || hasAppend || hasPlus) {
            return LockMode::Exclusive;
        }
        
        // Read-only mode
        return LockMode::Shared;
    }
    
    /// Apply file lock based on lock mode
    bool applyLock(LockMode mode) {
        if (mode == LockMode::None || !file_) {
            lockMode_ = LockMode::None;
            return true;
        }
        
#ifdef _WIN32
        int fd = _fileno(file_);
        if (fd == -1) return true; // Can't get fd, proceed without lock
        
        if (mode == LockMode::Exclusive) {
            // Windows _locking() only supports exclusive locks
            // _LK_NBLCK: Non-blocking exclusive lock
            if (_locking(fd, _LK_NBLCK, 0x7FFFFFFF) != 0) {
                return false; // Lock failed
            }
            lockMode_ = LockMode::Exclusive;
        } else {
            // Windows doesn't support shared locks via _locking()
            // For read-only, we don't lock to allow concurrent reads
            lockMode_ = LockMode::None;
        }
        return true;
#else
        int fd = fileno(file_);
        if (fd == -1) return true; // Can't get fd, proceed without lock
        
        int lockFlags = 0;
        switch (mode) {
            case LockMode::Shared:
                lockFlags = LOCK_SH | LOCK_NB;
                break;
            case LockMode::Exclusive:
                lockFlags = LOCK_EX | LOCK_NB;
                break;
            case LockMode::None:
                return true;
        }
        
        if (flock(fd, lockFlags) != 0) {
            return false; // Lock failed
        }
        lockMode_ = mode;
        return true;
#endif
    }
};

/// Simple string builder using C-style allocation
class StringBuilder {
public:
    StringBuilder() = default;
    
    explicit StringBuilder(size_t initialCapacity) {
        reserve(initialCapacity);
    }
    
    ~StringBuilder() {
        if (buffer_) {
            free(buffer_);
        }
    }
    
    // Non-copyable but movable
    StringBuilder(const StringBuilder&) = delete;
    StringBuilder& operator=(const StringBuilder&) = delete;
    StringBuilder(StringBuilder&& other) noexcept 
        : buffer_(other.buffer_), size_(other.size_), capacity_(other.capacity_) {
        other.buffer_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    
    StringBuilder& operator=(StringBuilder&& other) noexcept {
        if (this != &other) {
            if (buffer_) {
                free(buffer_);
            }
            buffer_ = other.buffer_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.buffer_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }
    
    void reserve(size_t newCapacity) {
        if (newCapacity <= capacity_) return;
        
        char* newBuffer = static_cast<char*>(realloc(buffer_, newCapacity));
        if (!newBuffer) return; // Out of memory
        
        buffer_ = newBuffer;
        capacity_ = newCapacity;
    }
    
    void append(const char* str) {
        if (!str) return;
        
        size_t len = strlen(str);
        append(str, len);
    }
    
    void append(const char* str, size_t len) {
        if (!str || len == 0) return;
        
        size_t newSize = size_ + len;
        if (newSize > capacity_) {
            size_t newCapacity = capacity_ ? capacity_ * 2 : 64;
            while (newCapacity < newSize) {
                newCapacity *= 2;
            }
            reserve(newCapacity);
        }
        
        if (buffer_ && size_ + len <= capacity_) {
            memcpy(buffer_ + size_, str, len);
            size_ = newSize;
        }
    }
    
    void append(const std::string& str) {
        append(str.data(), str.size());
    }
    
    void append(char c) {
        append(&c, 1);
    }
    
    template<typename... Args>
    bool appendFormat(const char* format, Args... args) {
        if (!format) return false;
        
        // First, try with a small buffer
        char smallBuf[256];
        int needed = snprintf(smallBuf, sizeof(smallBuf), format, args...);
        
        if (needed < 0) return false;
        
        if (static_cast<size_t>(needed) < sizeof(smallBuf)) {
            append(smallBuf, static_cast<size_t>(needed));
            return true;
        }
        
        // Need larger buffer
        size_t requiredSize = static_cast<size_t>(needed) + 1;
        reserve(size_ + requiredSize);
        
        if (!buffer_) return false;
        
        int result = snprintf(buffer_ + size_, requiredSize, format, args...);
        if (result > 0) {
            size_ += static_cast<size_t>(result);
            return true;
        }
        
        return false;
    }
    
    void clear() {
        size_ = 0;
    }
    
    const char* data() const {
        return buffer_ ? buffer_ : "";
    }
    
    size_t size() const {
        return size_;
    }
    
    bool empty() const {
        return size_ == 0;
    }
    
    std::string toString() const {
        return std::string(data(), size());
    }

private:
    char* buffer_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
};

} // namespace aiSocks
