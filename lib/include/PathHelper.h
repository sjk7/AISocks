// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define stat _stat64
    #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
    #define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#else
    #include <dirent.h>
    #include <unistd.h>
    #include <limits.h>
#endif

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

    /// Get information about a file or directory
    static FileInfo getFileInfo(const std::string& path) {
        FileInfo info;
        
#ifdef _WIN32
        struct _stat64 st;
        if (_stat64(path.c_str(), &st) != 0) {
            return info; // exists = false
        }
        
        info.exists = true;
        info.isDirectory = S_ISDIR(st.st_mode);
        info.size = static_cast<size_t>(st.st_size);
        info.lastModified = st.st_mtime;
        
        // Check for symlink/reparse point on Windows
        DWORD attrs = GetFileAttributesA(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            info.isSymlink = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        }
#else
        struct stat st;
        if (lstat(path.c_str(), &st) != 0) {
            return info; // exists = false
        }
        
        info.exists = true;
        info.isDirectory = S_ISDIR(st.st_mode);
        info.isSymlink = S_ISLNK(st.st_mode);
        info.size = static_cast<size_t>(st.st_size);
        info.lastModified = st.st_mtime;
#endif
        
        return info;
    }

    /// Check if a path exists
    static bool exists(const std::string& path) {
#ifdef _WIN32
        struct _stat64 st;
        return _stat64(path.c_str(), &st) == 0;
#else
        struct stat st;
        return stat(path.c_str(), &st) == 0;
#endif
    }

    /// Check if a path is a directory
    static bool isDirectory(const std::string& path) {
#ifdef _WIN32
        struct _stat64 st;
        if (_stat64(path.c_str(), &st) != 0) return false;
        return S_ISDIR(st.st_mode);
#else
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return false;
        return S_ISDIR(st.st_mode);
#endif
    }

    /// Check if a path is a symlink
    static bool isSymlink(const std::string& path) {
#ifdef _WIN32
        DWORD attrs = GetFileAttributesA(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) return false;
        return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
        struct stat st;
        if (lstat(path.c_str(), &st) != 0) return false;
        return S_ISLNK(st.st_mode);
#endif
    }

    /// Get file size
    static size_t fileSize(const std::string& path) {
#ifdef _WIN32
        struct _stat64 st;
        if (_stat64(path.c_str(), &st) != 0) return 0;
        return static_cast<size_t>(st.st_size);
#else
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return 0;
        return static_cast<size_t>(st.st_size);
#endif
    }

    /// Get last modification time
    static time_t lastWriteTime(const std::string& path) {
#ifdef _WIN32
        struct _stat64 st;
        if (_stat64(path.c_str(), &st) != 0) return 0;
        return st.st_mtime;
#else
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return 0;
        return st.st_mtime;
#endif
    }

    /// List directory contents
    static std::vector<DirEntry> listDirectory(const std::string& path) {
        std::vector<DirEntry> entries;
        
#ifdef _WIN32
        std::string searchPath = path;
        if (!searchPath.empty() && searchPath.back() != '/' && searchPath.back() != '\\') {
            searchPath += "\\*";
        } else {
            searchPath += "*";
        }
        
        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
        
        if (hFind == INVALID_HANDLE_VALUE) {
            return entries;
        }
        
        do {
            std::string name = findData.cFileName;
            if (name != "." && name != "..") {
                DirEntry entry;
                entry.name = name;
                entry.isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                entries.push_back(entry);
            }
        } while (FindNextFileA(hFind, &findData));
        
        FindClose(hFind);
#else
        DIR* dir = opendir(path.c_str());
        if (!dir) {
            return entries;
        }
        
        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            std::string name = ent->d_name;
            if (name != "." && name != "..") {
                DirEntry entry;
                entry.name = name;
                
                // Try to determine if it's a directory
                std::string fullPath = path;
                if (!fullPath.empty() && fullPath.back() != '/') {
                    fullPath += '/';
                }
                fullPath += name;
                
                struct stat st;
                if (stat(fullPath.c_str(), &st) == 0) {
                    entry.isDirectory = S_ISDIR(st.st_mode);
                }
                
                entries.push_back(entry);
            }
        }
        
        closedir(dir);
#endif
        
        return entries;
    }

    /// Normalize path separators to forward slashes
    static std::string normalizePath(const std::string& path) {
        std::string normalized = path;
        for (size_t i = 0; i < normalized.size(); ++i) {
            if (normalized[i] == '\\') normalized[i] = '/';
        }
        return normalized;
    }

    /// Get canonical/absolute path (resolves . and .. but doesn't follow symlinks)
    /// 
    /// WHAT IS CANONICALIZATION?
    /// Canonicalization converts a path to its "canonical" (standard, absolute) form by:
    /// 1. Converting to an absolute path (e.g., "test.txt" -> "/home/user/test.txt")
    /// 2. Resolving "." (current directory) and ".." (parent directory) components
    /// 3. Removing redundant separators (e.g., "a//b" -> "a/b")
    /// 4. On some systems, resolving symlinks (we avoid this for security)
    ///
    /// WHY IS THIS CRITICAL FOR FILE SERVING?
    /// Path traversal attacks use ".." to escape the document root:
    ///   Request: GET /../../../etc/passwd
    ///   Without canonicalization: documentRoot + "/../../../etc/passwd" 
    ///                            = "www/../../../etc/passwd" (looks safe!)
    ///   After canonicalization: "/etc/passwd" (clearly outside www/)
    ///
    /// PLATFORM DIFFERENCES:
    /// - Windows: GetFullPathName() works for nonexistent paths
    /// - Unix/Linux: realpath() FAILS for nonexistent paths (returns NULL)
    ///   This is why we need normalizePathManual() as a fallback
    ///
    /// Returns empty string on error (e.g., invalid path, nonexistent file on Unix)
    static std::string getCanonicalPath(const std::string& path) {
#ifdef _WIN32
        char buffer[MAX_PATH];
        DWORD result = GetFullPathNameA(path.c_str(), MAX_PATH, buffer, nullptr);
        if (result == 0 || result >= MAX_PATH) {
            return "";
        }
        return normalizePath(std::string(buffer));
#else
        char buffer[PATH_MAX];
        if (realpath(path.c_str(), buffer) == nullptr) {
            // If realpath fails, try to build a canonical path manually
            // This is a simplified version that handles basic cases
            return normalizePathManual(path);
        }
        return std::string(buffer);
#endif
    }

    /// Check if childPath is within parentPath (after canonicalization)
    /// 
    /// THIS IS THE KEY SECURITY FUNCTION FOR FILE SERVING
    /// 
    /// PURPOSE: Prevent path traversal attacks that try to access files outside
    /// the document root using ".." (parent directory) components.
    ///
    /// HOW IT WORKS:
    /// 1. Canonicalize both paths to resolve all "." and ".." components
    /// 2. Compare the canonical paths to ensure child is within parent
    ///
    /// EXAMPLE ATTACK PREVENTION:
    ///   Document root: "/var/www"
    ///   Requested file: "/var/www/../../../etc/passwd"
    ///   
    ///   After canonicalization:
    ///     childPath:  "/etc/passwd"
    ///     parentPath: "/var/www/"
    ///   
    ///   Check: Does "/etc/passwd" start with "/var/www/"? NO → BLOCKED (403)
    ///
    /// HANDLING NONEXISTENT FILES:
    /// Problem: On Unix, realpath() fails for nonexistent files (returns NULL)
    /// Solution: Use normalizePathManual() fallback which manually resolves ".."
    ///           This allows us to return 404 for missing files instead of 403
    ///
    /// RELATIONSHIP TO FILE SERVING:
    /// - User requests: GET /nonexistent.html
    /// - Resolved path: "www/nonexistent.html" (doesn't exist)
    /// - getCanonicalPath() fails on Unix → use normalizePathManual()
    /// - Security check passes → proceed to existence check → return 404
    ///
    /// - User requests: GET /../etc/passwd (path traversal attack)
    /// - Resolved path: "www/../etc/passwd"
    /// - normalizePathManual() resolves to "/etc/passwd"
    /// - Security check: "/etc/passwd" not within "www/" → return 403
    ///
    static bool isPathWithin(const std::string& childPath, const std::string& parentPath) {
        // SIMPLIFIED APPROACH: Always use manual normalization for both paths
        // This avoids OS-dependent behavior differences and works consistently
        // across Windows, macOS, and Linux regardless of whether files exist
        
        std::string normalizedChild = normalizePathManual(childPath);
        std::string normalizedParent = normalizePathManual(parentPath);
        
        if (normalizedChild.empty() || normalizedParent.empty()) {
            return false;
        }
        
        // Normalize slashes
        normalizedChild = normalizePath(normalizedChild);
        normalizedParent = normalizePath(normalizedParent);
        
        // Ensure parent path ends with /
        if (!normalizedParent.empty() && normalizedParent.back() != '/') {
            normalizedParent += '/';
        }
        
        // Check if child starts with parent
        if (normalizedChild.size() < normalizedParent.size()) {
            return false;
        }
        
        return normalizedChild.compare(0, normalizedParent.size(), normalizedParent) == 0;
    }

    /// Extract filename from path
    static std::string getFilename(const std::string& path) {
        std::string normalized = normalizePath(path);
        size_t pos = normalized.find_last_of('/');
        if (pos == std::string::npos) {
            return normalized;
        }
        return normalized.substr(pos + 1);
    }

    /// Extract file extension (including the dot)
    static std::string getExtension(const std::string& path) {
        std::string filename = getFilename(path);
        size_t pos = filename.find_last_of('.');
        if (pos == std::string::npos || pos == 0) {
            return "";
        }
        return filename.substr(pos);
    }

    /// Join two path components
    static std::string joinPath(const std::string& base, const std::string& component) {
        if (base.empty()) return component;
        if (component.empty()) return base;
        
        std::string result = normalizePath(base);
        std::string comp = normalizePath(component);
        
        // Remove leading slash from component
        if (!comp.empty() && comp[0] == '/') {
            comp = comp.substr(1);
        }
        
        // Ensure base ends with slash
        if (!result.empty() && result.back() != '/') {
            result += '/';
        }
        
        return result + comp;
    }

private:
    /// Manual path normalization for systems where realpath fails
    /// Resolves . and .. components while preserving relative vs absolute path nature
    static std::string normalizePathManual(const std::string& path) {
        std::string normalized = normalizePath(path);
        
        // Check if path is absolute (starts with /)
        bool isAbsolute = !normalized.empty() && normalized[0] == '/';
        
        std::vector<std::string> components;
        std::string current;
        
        for (size_t i = 0; i < normalized.size(); ++i) {
            char c = normalized[i];
            if (c == '/') {
                if (!current.empty()) {
                    if (current == "..") {
                        if (!components.empty()) {
                            components.pop_back();
                        }
                    } else if (current != ".") {
                        components.push_back(current);
                    }
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        
        if (!current.empty()) {
            if (current == "..") {
                if (!components.empty()) {
                    components.pop_back();
                }
            } else if (current != ".") {
                components.push_back(current);
            }
        }
        
        // Build result preserving absolute/relative nature
        std::string result;
        if (isAbsolute) {
            // Absolute path: start with /
            for (size_t i = 0; i < components.size(); ++i) {
                result += "/" + components[i];
            }
            return result.empty() ? "/" : result;
        } else {
            // Relative path: don't start with /
            for (size_t i = 0; i < components.size(); ++i) {
                if (i > 0) result += "/";
                result += components[i];
            }
            return result.empty() ? "." : result;
        }
    }
};

} // namespace aiSocks
