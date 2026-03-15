// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#ifndef _WIN32
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#endif

#include "FileIO.h"

#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <share.h>
#include <sys/locking.h>
#include <windows.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#else
#include <sys/file.h>
#include <unistd.h>
#endif

namespace aiSocks {

namespace {
    std::string binaryMode_(const char* mode) {
        if (!mode || !*mode) return {};
        std::string out(mode);

        // Force binary semantics across platforms; POSIX ignores 'b', Windows
        // treats it as required for byte-exact reads/writes.
        out.erase(std::remove(out.begin(), out.end(), 't'), out.end());
        if (out.find('b') == std::string::npos) out.push_back('b');
        return out;
    }
} // namespace

// ---------------------------------------------------------------------------
// File
// ---------------------------------------------------------------------------

File::File(const char* filename, const char* mode) {
    open(filename, mode);
}

File::~File() {
    close();
}

File::File(File&& other) noexcept
    : file_(other.file_), lockMode_(other.lockMode_) {
    other.file_ = nullptr;
    other.lockMode_ = LockMode::None;
}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close();
        file_ = other.file_;
        lockMode_ = other.lockMode_;
        other.file_ = nullptr;
        other.lockMode_ = LockMode::None;
    }
    return *this;
}

bool File::open(const char* filename, const char* mode) {
    close();

    LockMode lockMode = determineLockMode(mode);
    const std::string modeStr = binaryMode_(mode);
    if (modeStr.empty()) return false;
    const char* openMode = modeStr.c_str();

#ifdef _WIN32
    // Use _fsopen with _SH_DENYNO so that write-mode opens (e.g. log files)
    // allow concurrent reads from the same process, matching POSIX behaviour.
    file_ = _fsopen(filename, openMode, _SH_DENYNO);
    if (!file_) return false;
#else
    // For read-only opens, avoid following a final-path symlink to reduce
    // TOCTOU exposure when serving files from untrusted paths.
#ifdef O_NOFOLLOW
    const bool hasWrite = (mode && strchr(mode, 'w') != nullptr);
    const bool hasAppend = (mode && strchr(mode, 'a') != nullptr);
    const bool hasPlus = (mode && strchr(mode, '+') != nullptr);
    const bool readOnly = !hasWrite && !hasAppend && !hasPlus;

    if (readOnly) {
        const int fd = ::open(filename, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd >= 0) {
            file_ = fdopen(fd, openMode);
            if (!file_) {
                ::close(fd);
                return false;
            }
        } else {
            return false;
        }
    } else
#endif
    {
        file_ = fopen(filename, openMode);
        if (!file_) return false;
    }
#endif

    if (!applyLock(lockMode)) {
        fclose(file_);
        file_ = nullptr;
        return false;
    }
    return true;
}

void File::close() {
    if (file_) {
        releaseLock();
        fclose(file_);
        file_ = nullptr;
        lockMode_ = LockMode::None;
    }
}

void File::releaseLock() {
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

bool File::isOpen() const {
    return file_ != nullptr;
}

bool File::eof() const {
    return file_ ? feof(file_) != 0 : true;
}

size_t File::read(void* buffer, size_t size, size_t count) {
    return file_ ? fread(buffer, size, count, file_) : 0;
}

size_t File::write(const void* buffer, size_t size, size_t count) {
    return file_ ? fwrite(buffer, size, count, file_) : 0;
}

bool File::writeString(const char* str) {
    if (!file_ || !str) return false;
    size_t len = strlen(str);
    return write(str, 1, len) == len;
}

bool File::writeString(const std::string& str) {
    return write(str.data(), 1, str.size()) == str.size();
}

bool File::printf(const char* format, ...) {
    if (!file_) return false;

    va_list args;
    va_start(args, format);
    int result = vfprintf(file_, format, args);
    va_end(args);

    return result >= 0;
}

bool File::flush() {
    return file_ ? fflush(file_) == 0 : false;
}

#ifdef _WIN32
bool File::seek(long long offset, int whence) {
    return file_ ? _fseeki64(file_, offset, whence) == 0 : false;
}

long long File::tell() {
    return file_ ? _ftelli64(file_) : -1;
}
#else
bool File::seek(int64_t offset, int whence) {
    return file_ ? fseeko(file_, offset, whence) == 0 : false;
}

int64_t File::tell() {
    return file_ ? ftello(file_) : -1;
}
#endif

size_t File::size() {
    if (!file_) return 0;

#ifdef _WIN32
    long long current = tell();
#else
    int64_t current = tell();
#endif
    if (current < 0) return 0;

    if (!seek(0, SEEK_END)) return 0;
#ifdef _WIN32
    long long fileSize = tell();
#else
    int64_t fileSize = tell();
#endif

    seek(current, SEEK_SET);
    return fileSize > 0 ? static_cast<size_t>(fileSize) : 0;
}

File::FileInfo File::getInfoFromDescriptor() const {
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

    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (h != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION fileInfo;
        if (GetFileInformationByHandle(h, &fileInfo)) {
            info.isSymlink
                = (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                != 0;
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

std::vector<char> File::readAll() {
    std::vector<char> result;
    if (!file_) return result;

    size_t fileSize = size();
    if (fileSize == 0) return result;

    result.resize(fileSize);
    size_t bytesRead = read(result.data(), 1, fileSize);
    result.resize(bytesRead);

    return result;
}

FILE* File::get() const {
    return file_;
}

File::operator bool() const {
    return isOpen();
}

LockMode File::determineLockMode(const char* mode) {
    if (!mode) return LockMode::None;

    bool hasWrite = (strchr(mode, 'w') != nullptr);
    bool hasAppend = (strchr(mode, 'a') != nullptr);
    bool hasPlus = (strchr(mode, '+') != nullptr);

    if (hasWrite || hasAppend || hasPlus) {
        return LockMode::Exclusive;
    }

    return LockMode::Shared;
}

bool File::applyLock(LockMode mode) {
    if (mode == LockMode::None || !file_) {
        lockMode_ = LockMode::None;
        return true;
    }

#ifdef _WIN32
    int fd = _fileno(file_);
    if (fd == -1) return true;

    if (mode == LockMode::Exclusive) {
        if (_locking(fd, _LK_NBLCK, 0x7FFFFFFF) != 0) {
            return false;
        }
        lockMode_ = LockMode::Exclusive;
    } else {
        lockMode_ = LockMode::None;
    }
    return true;
#else
    int fd = fileno(file_);
    if (fd == -1) return true;

    int lockFlags = 0;
    switch (mode) {
        case LockMode::Shared: lockFlags = LOCK_SH | LOCK_NB; break;
        case LockMode::Exclusive: lockFlags = LOCK_EX | LOCK_NB; break;
        case LockMode::None: return true;
    }

    if (flock(fd, lockFlags) != 0) {
        return false;
    }
    lockMode_ = mode;
    return true;
#endif
}

} // namespace aiSocks
