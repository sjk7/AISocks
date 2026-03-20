// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "PathHelper.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <cerrno>
#include <cstdlib>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define stat _stat64
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#else
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#endif

namespace aiSocks {

PathHelper::FileInfo PathHelper::getFileInfo(const std::string& path) {
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

bool PathHelper::exists(const std::string& path) {
#ifdef _WIN32
    struct _stat64 st;
    return _stat64(path.c_str(), &st) == 0;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

bool PathHelper::isDirectory(const std::string& path) {
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

bool PathHelper::isSymlink(const std::string& path) {
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

// Advances `i` past any leading '/' separators, extracts the next path
// component, and advances `i` to just after it.  Returns an empty view when
// the string is exhausted.
static std::string_view nextPathComponent_(const std::string& rel, size_t& i) {
    while (i < rel.size() && rel[i] == '/') ++i;
    if (i >= rel.size()) return {};
    const size_t j = rel.find('/', i);
    const std::string_view comp = (j == std::string::npos)
        ? std::string_view(rel).substr(i)
        : std::string_view(rel).substr(i, j - i);
    i = (j == std::string::npos) ? rel.size() : j + 1;
    return comp;
}

bool PathHelper::hasSymlinkComponentWithin(
    const std::string& fullPath, const std::string& rootPath) {
    std::string root = normalizePath(rootPath);
    std::string path = normalizePath(fullPath);

    if (!root.empty() && root.back() != '/') root.push_back('/');
    if (root.empty()) return true;
    if (path.size() < root.size()) return true;
    if (path.compare(0, root.size(), root) != 0) return true;

    std::string current = root;
    // NOTE: !current.empty() is always true here (root was validated above),
    // but kept for defensive programming clarity.
    if (!current.empty() && current.back() == '/') //-V560
        current.pop_back(); //-V560 //-V560

    const std::string rel = path.substr(root.size());
    size_t i = 0;
    while (true) {
        const std::string_view comp = nextPathComponent_(rel, i);
        if (comp.empty()) break;

        current += "/";
        current.append(comp.data(), comp.size());

        // Only evaluate symlink status for components that actually exist.
        if (!exists(current)) return false;
        if (isSymlink(current)) return true;
    }

    return false;
}

size_t PathHelper::fileSize(const std::string& path) {
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

time_t PathHelper::lastWriteTime(const std::string& path) {
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

#ifdef _WIN32
static std::vector<PathHelper::DirEntry> listDirectoryWindows_(
    const std::string& path) {
    std::vector<PathHelper::DirEntry> entries;
    std::string searchPath = path;
    if (!searchPath.empty() && searchPath.back() != '/'
        && searchPath.back() != '\\') {
        searchPath += "\\*";
    } else {
        searchPath += "*";
    }

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return entries;

    do {
        std::string name = findData.cFileName;
        if (name != "." && name != "..") {
            PathHelper::DirEntry entry;
            entry.name = std::move(name);
            entry.isDirectory
                = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            entries.push_back(entry);
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    return entries;
}
#else
static std::vector<PathHelper::DirEntry> listDirectoryUnix_(
    const std::string& path) {
    std::vector<PathHelper::DirEntry> entries;
    DIR* dir = opendir(path.c_str());
    if (!dir) return entries;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name != "." && name != "..") {
            PathHelper::DirEntry entry;
            entry.name = name;
            std::string fullPath = path;
            if (!fullPath.empty() && fullPath.back() != '/') fullPath += '/';
            fullPath += name;
            struct stat st;
            if (stat(fullPath.c_str(), &st) == 0)
                entry.isDirectory = S_ISDIR(st.st_mode);
            entries.push_back(entry);
        }
    }

    closedir(dir);
    return entries;
}
#endif

std::vector<PathHelper::DirEntry> PathHelper::listDirectory(
    const std::string& path) {
#ifdef _WIN32
    return listDirectoryWindows_(path);
#else
    return listDirectoryUnix_(path);
#endif
}

std::string PathHelper::normalizePath(const std::string& path) {
    std::string normalized = path;
    for (size_t i = 0; i < normalized.size(); ++i) {
        if (normalized[i] == '\\') normalized[i] = '/';
    }
    return normalized;
}

std::string PathHelper::getCanonicalPath(const std::string& path) {
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
        return normalizePathManual(path);
    }
    return std::string(buffer);
#endif
}

bool PathHelper::isPathWithin(
    const std::string& childPath, const std::string& parentPath) {
    std::string normalizedChild = normalizePathManual(childPath);
    std::string normalizedParent = normalizePathManual(parentPath);

    if (normalizedChild.empty() || normalizedParent.empty()) {
        return false;
    }

    normalizedChild = normalizePath(normalizedChild);
    normalizedParent = normalizePath(normalizedParent);

    if (!normalizedParent.empty() && normalizedParent.back() != '/') {
        normalizedParent += '/';
    }

    if (normalizedChild.size() < normalizedParent.size()) {
        return false;
    }

    return normalizedChild.compare(0, normalizedParent.size(), normalizedParent)
        == 0;
}

std::string PathHelper::getFilename(const std::string& path) {
    std::string normalized = normalizePath(path);
    size_t pos = normalized.find_last_of('/');
    if (pos == std::string::npos) {
        return normalized;
    }
    return normalized.substr(pos + 1);
}

std::string PathHelper::getExtension(const std::string& path) {
    std::string filename = getFilename(path);
    size_t pos = filename.find_last_of('.');
    if (pos == std::string::npos || pos == 0) {
        return "";
    }
    return filename.substr(pos);
}

std::string PathHelper::joinPath(
    const std::string& base, const std::string& component) {
    if (base.empty()) return component;
    if (component.empty()) return base;

    std::string result = normalizePath(base);
    std::string comp = normalizePath(component);

    if (!comp.empty() && comp[0] == '/') {
        comp = comp.substr(1);
    }

    if (!result.empty() && result.back() != '/') {
        result += '/';
    }

    return result + comp;
}

namespace {

bool createDirectorySingle_(const std::string& path) {
#ifdef _WIN32
    if (_mkdir(path.c_str()) == 0) return true;
    if (errno == EEXIST) return PathHelper::isDirectory(path);
    return false;
#else
    if (mkdir(path.c_str(), 0755) == 0) return true;
    if (errno == EEXIST) return PathHelper::isDirectory(path);
    return false;
#endif
}

bool removeFileOrSymlink_(const std::string& path) {
#ifdef _WIN32
    return DeleteFileA(path.c_str()) != 0;
#else
    return unlink(path.c_str()) == 0;
#endif
}

bool removeDirectorySingle_(const std::string& path) {
#ifdef _WIN32
    return RemoveDirectoryA(path.c_str()) != 0;
#else
    return rmdir(path.c_str()) == 0;
#endif
}

} // namespace

bool PathHelper::createDirectories(const std::string& path) {
    const std::string normalized = normalizePath(path);
    if (normalized.empty()) return false;
    if (exists(normalized)) return isDirectory(normalized);

    std::string current;
    size_t index = 0;

    // Preserve Windows drive prefix (e.g., C:)
    if (normalized.size() >= 2 && normalized[1] == ':') {
        current = normalized.substr(0, 2);
        index = 2;
    }

    // Preserve absolute root slash
    if (index < normalized.size() && normalized[index] == '/') {
        current += '/';
        ++index;
    }

    while (index < normalized.size()) {
        const size_t next = normalized.find('/', index);
        const std::string_view component = (next == std::string::npos)
            ? std::string_view(normalized).substr(index)
            : std::string_view(normalized).substr(index, next - index);

        if (!component.empty() && component != ".") {
            if (!current.empty() && current.back() != '/') current += '/';
            current.append(component.data(), component.size());
            if (!exists(current) && !createDirectorySingle_(current)) {
                return false;
            }
        }

        if (next == std::string::npos) break;
        index = next + 1;
    }

    return exists(normalized) && isDirectory(normalized);
}

bool PathHelper::removeAll(const std::string& path) {
    const std::string normalized = normalizePath(path);
    if (normalized.empty()) return false;
    if (!exists(normalized)) return true;

    const FileInfo info = getFileInfo(normalized);
    if (!info.exists) return true;

    if (info.isSymlink || !info.isDirectory) {
        return removeFileOrSymlink_(normalized);
    }

    const std::vector<DirEntry> entries = listDirectory(normalized);
    for (const DirEntry& entry : entries) {
        const std::string child = joinPath(normalized, entry.name);
        if (!removeAll(child)) return false;
    }

    return removeDirectorySingle_(normalized);
}

std::string PathHelper::tempDirectory() {
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    const DWORD len = GetTempPathA(MAX_PATH, buf);
    if (len == 0 || len >= MAX_PATH) return ".";
    return normalizePath(std::string(buf));
#else
    const char* tmpdir = std::getenv("TMPDIR");
    if (tmpdir != nullptr && tmpdir[0] != '\0') {
        return normalizePath(std::string(tmpdir));
    }
    return "/tmp";
#endif
}

std::string PathHelper::normalizePathManual(const std::string& path) {
    std::string normalized = normalizePath(path);

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

    std::string result;
    if (isAbsolute) {
        for (size_t i = 0; i < components.size(); ++i) {
            result += "/" + components[i];
        }
        return result.empty() ? "/" : result;
    } else {
        for (size_t i = 0; i < components.size(); ++i) {
            if (i > 0) result += "/";
            result += components[i];
        }
        return result.empty() ? "." : result;
    }
}

} // namespace aiSocks
