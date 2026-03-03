// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <cstdarg>
#include <cerrno>

namespace aiSocks {

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
#ifdef _WIN32
        errno_t err = fopen_s(&file_, filename, mode);
        return err == 0 && file_ != nullptr;
#else
        file_ = std::fopen(filename, mode);
        return file_ != nullptr;
#endif
    }
    
    void close() {
        if (file_) {
            std::fclose(file_);
            file_ = nullptr;
        }
    }
    
    bool isOpen() const {
        return file_ != nullptr;
    }
    
    bool eof() const {
        return file_ ? std::feof(file_) != 0 : true;
    }
    
    size_t read(void* buffer, size_t size, size_t count) {
        return file_ ? std::fread(buffer, size, count, file_) : 0;
    }
    
    size_t write(const void* buffer, size_t size, size_t count) {
        return file_ ? std::fwrite(buffer, size, count, file_) : 0;
    }
    
    bool writeString(const char* str) {
        if (!file_ || !str) return false;
        size_t len = std::strlen(str);
        return write(str, 1, len) == len;
    }
    
    bool writeString(const std::string& str) {
        return write(str.data(), 1, str.size()) == str.size();
    }
    
    bool printf(const char* format, ...) {
        if (!file_) return false;
        
        va_list args;
        va_start(args, format);
        int result = std::vfprintf(file_, format, args);
        va_end(args);
        
        return result >= 0;
    }
    
    bool flush() {
        return file_ ? std::fflush(file_) == 0 : false;
    }
    
    bool seek(long offset, int whence) {
        return file_ ? std::fseek(file_, offset, whence) == 0 : false;
    }
    
    long tell() {
        return file_ ? std::ftell(file_) : -1;
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
            std::free(buffer_);
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
                std::free(buffer_);
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
        
        char* newBuffer = static_cast<char*>(std::realloc(buffer_, newCapacity));
        if (!newBuffer) return; // Out of memory
        
        buffer_ = newBuffer;
        capacity_ = newCapacity;
    }
    
    void append(const char* str) {
        if (!str) return;
        
        size_t len = std::strlen(str);
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
            std::memcpy(buffer_ + size_, str, len);
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
        int needed = std::snprintf(smallBuf, sizeof(smallBuf), format, args...);
        
        if (needed < 0) return false;
        
        if (static_cast<size_t>(needed) < sizeof(smallBuf)) {
            append(smallBuf, static_cast<size_t>(needed));
            return true;
        }
        
        // Need larger buffer
        size_t requiredSize = static_cast<size_t>(needed) + 1;
        reserve(size_ + requiredSize);
        
        if (!buffer_) return false;
        
        int result = std::snprintf(buffer_ + size_, requiredSize, format, args...);
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
