// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com



#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace aiSocks {

/// Helper class for file system operations using C library functions
/// Replaces std::filesystem to improve compile times
class PathHelper {
    public:
    struct FileInfo {
        bool exists = false;
        bool isDirectory = false;
        bool isSymlink = false;
        size_t size = 0;
        time_t lastModified = 0;
    };

    struct DirEntry {
        std::string name;
        bool isDirectory = false;
    };

    static FileInfo getFileInfo(const std::string& path);
    static bool exists(const std::string& path);
    static bool isDirectory(const std::string& path);
    static bool isSymlink(const std::string& path);

    /// Returns true if any existing path component under rootPath is a symlink
    /// (POSIX) or reparse-point (Windows).
    static bool hasSymlinkComponentWithin(
        const std::string& fullPath, const std::string& rootPath);

    static size_t fileSize(const std::string& path);
    static time_t lastWriteTime(const std::string& path);
    static std::vector<DirEntry> listDirectory(const std::string& path);

    /// Normalize path separators to forward slashes
    static std::string normalizePath(const std::string& path);

    /// Get canonical/absolute path (resolves . and .. but doesn't follow
    /// symlinks)
    static std::string getCanonicalPath(const std::string& path);

    /// Check if childPath is within parentPath (after normalization)
    static bool isPathWithin(
        const std::string& childPath, const std::string& parentPath);

    static std::string getFilename(const std::string& path);
    static std::string getExtension(const std::string& path);
    static std::string joinPath(
        const std::string& base, const std::string& component);

    /// Create a directory and any missing parents. Returns true on success.
    static bool createDirectories(const std::string& path);

    /// Remove a file or directory tree recursively. Missing paths are treated
    /// as success.
    static bool removeAll(const std::string& path);

    /// Return a writable temporary directory path.
    static std::string tempDirectory();

    private:
    static std::string normalizePathManual(const std::string& path);
};

} // namespace aiSocks
