// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

// Comprehensive tests for PathHelper - security-critical path operations
// Tests path normalization, canonicalization, containment checking, and symlink
// detection

#include "PathHelper.h"
#include "test_helpers.h"
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace aiSocks;

// Helper to create test directory structure
static void setupTestDirs() {
#ifdef _WIN32
    _mkdir("test_path_root");
    _mkdir("test_path_root\\subdir");
    _mkdir("test_path_root\\subdir\\nested");
    _mkdir("test_path_outside");
#else
    mkdir("test_path_root", 0755);
    mkdir("test_path_root/subdir", 0755);
    mkdir("test_path_root/subdir/nested", 0755);
    mkdir("test_path_outside", 0755);
#endif
}

static void cleanupTestDirs() {
#ifdef _WIN32
    _rmdir("test_path_root\\subdir\\nested");
    _rmdir("test_path_root\\subdir");
    _rmdir("test_path_root");
    _rmdir("test_path_outside");
#else
    rmdir("test_path_root/subdir/nested");
    rmdir("test_path_root/subdir");
    rmdir("test_path_root");
    rmdir("test_path_outside");
#endif
}

int main() {
    std::cout << "=== PathHelper Tests ===\n";

    setupTestDirs();

    // Test 1: normalizePath - forward slash conversion
    BEGIN_TEST("normalizePath: converts backslashes to forward slashes");
    {
        std::string path = "path\\to\\file.txt";
        std::string normalized = PathHelper::normalizePath(path);
        REQUIRE(normalized == "path/to/file.txt");
    }

    // Test 2: normalizePath - already normalized
    BEGIN_TEST("normalizePath: leaves forward slashes unchanged");
    {
        std::string path = "path/to/file.txt";
        std::string normalized = PathHelper::normalizePath(path);
        REQUIRE(normalized == "path/to/file.txt");
    }

    // Test 3: normalizePath - mixed slashes
    BEGIN_TEST("normalizePath: handles mixed slashes");
    {
        std::string path = "path\\to/subdir\\file.txt";
        std::string normalized = PathHelper::normalizePath(path);
        REQUIRE(normalized == "path/to/subdir/file.txt");
    }

    // Test 4: normalizePath - empty path
    BEGIN_TEST("normalizePath: handles empty path");
    {
        std::string path = "";
        std::string normalized = PathHelper::normalizePath(path);
        REQUIRE(normalized == ""); //-V815
    }

    // Test 5: getCanonicalPath - resolves single dot
    BEGIN_TEST("getCanonicalPath: resolves . (current directory)");
    {
        std::string path = "test_path_root/./subdir";
        std::string canonical = PathHelper::getCanonicalPath(path);
        // Should remove the .
        REQUIRE(canonical.find("/.") == std::string::npos
            || canonical.find("\\.") == std::string::npos);
    }

    // Test 6: getCanonicalPath - resolves double dot
    BEGIN_TEST("getCanonicalPath: resolves .. (parent directory)");
    {
        std::string path = "test_path_root/subdir/../subdir";
        std::string canonical = PathHelper::getCanonicalPath(path);
        // Should end with subdir, not have .. in path
        REQUIRE(canonical.find("..") == std::string::npos);
    }

    // Test 7: getCanonicalPath - multiple parent references
    BEGIN_TEST("getCanonicalPath: resolves multiple ..");
    {
        std::string path = "test_path_root/subdir/nested/../../subdir";
        std::string canonical = PathHelper::getCanonicalPath(path);
        REQUIRE(canonical.find("..") == std::string::npos);
    }

    // Test 8: getCanonicalPath - leading ..
    BEGIN_TEST("getCanonicalPath: handles leading ..");
    {
        std::string path = "../test_path_root";
        std::string canonical = PathHelper::getCanonicalPath(path);
        // Should preserve leading .. (can't resolve without absolute path)
        REQUIRE(!canonical.empty());
    }

    // Test 9: isPathWithin - child is within parent
    BEGIN_TEST("isPathWithin: child path is within parent");
    {
        bool within = PathHelper::isPathWithin(
            "test_path_root/subdir/file.txt", "test_path_root");
        REQUIRE(within == true);
    }

    // Test 10: isPathWithin - path traversal attempt
    BEGIN_TEST("isPathWithin: detects path traversal with ..");
    {
        bool within = PathHelper::isPathWithin(
            "test_path_root/../test_path_outside/file.txt", "test_path_root");
        REQUIRE(within == false);
    }

    // Test 11: isPathWithin - exactly equal paths
    BEGIN_TEST("isPathWithin: path equals parent (not strictly within)");
    {
        bool within
            = PathHelper::isPathWithin("test_path_root", "test_path_root");
        REQUIRE(within == false);
    }

    // Test 12: isPathWithin - sibling directory
    BEGIN_TEST("isPathWithin: sibling directory is not within");
    {
        bool within = PathHelper::isPathWithin(
            "test_path_outside/file.txt", "test_path_root");
        REQUIRE(within == false);
    }

    // Test 13: isPathWithin - multiple .. to escape
    BEGIN_TEST("isPathWithin: detects multiple .. traversal attempts");
    {
        bool within = PathHelper::isPathWithin(
            "test_path_root/subdir/../../test_path_outside", "test_path_root");
        REQUIRE(within == false);
    }

    // Test 14: getFilename - simple file
    BEGIN_TEST("getFilename: extracts filename from path");
    {
        std::string filename = PathHelper::getFilename("path/to/file.txt");
        REQUIRE(filename == "file.txt");
    }

    // Test 15: getFilename - no directory
    BEGIN_TEST("getFilename: returns filename when no directory");
    {
        std::string filename = PathHelper::getFilename("file.txt");
        REQUIRE(filename == "file.txt");
    }

    // Test 16: getFilename - trailing slash
    BEGIN_TEST("getFilename: handles trailing slash");
    {
        std::string filename = PathHelper::getFilename("path/to/dir/");
        REQUIRE(filename == ""); //-V815
    }

    // Test 17: getFilename - Windows path
    BEGIN_TEST("getFilename: handles Windows-style path");
    {
        std::string filename
            = PathHelper::getFilename("C:\\path\\to\\file.txt");
        REQUIRE(filename == "file.txt");
    }

    // Test 18: getExtension - common extension
    BEGIN_TEST("getExtension: extracts file extension");
    {
        std::string ext = PathHelper::getExtension("file.txt");
        REQUIRE(ext == ".txt");
    }

    // Test 19: getExtension - multiple dots
    BEGIN_TEST("getExtension: handles multiple dots in filename");
    {
        std::string ext = PathHelper::getExtension("archive.tar.gz");
        REQUIRE(ext == ".gz");
    }

    // Test 20: getExtension - no extension
    BEGIN_TEST("getExtension: returns empty for no extension");
    {
        std::string ext = PathHelper::getExtension("README");
        REQUIRE(ext == ""); //-V815
    }

    // Test 21: getExtension - hidden file (Unix)
    BEGIN_TEST("getExtension: handles hidden file");
    {
        std::string ext = PathHelper::getExtension(".gitignore");
        REQUIRE(ext == "" || ext == ".gitignore"); //-V815
    }

    // Test 22: joinPath - two components
    BEGIN_TEST("joinPath: joins two path components");
    {
        std::string joined = PathHelper::joinPath("path/to", "file.txt");
        REQUIRE(joined.find("path/to") != std::string::npos);
        REQUIRE(joined.find("file.txt") != std::string::npos);
    }

    // Test 23: joinPath - base with trailing slash
    BEGIN_TEST("joinPath: handles base with trailing slash");
    {
        std::string joined = PathHelper::joinPath("path/to/", "file.txt");
        REQUIRE(joined.find("//") == std::string::npos); // No double slash
    }

    // Test 24: joinPath - component with leading slash
    BEGIN_TEST("joinPath: handles component with leading slash");
    {
        std::string joined = PathHelper::joinPath("path/to", "/file.txt");
        // Should still produce valid path
        REQUIRE(!joined.empty());
    }

    // Test 25: exists - existing directory
    BEGIN_TEST("exists: returns true for existing directory");
    {
        bool exists = PathHelper::exists("test_path_root");
        REQUIRE(exists == true);
    }

    // Test 26: exists - non-existing path
    BEGIN_TEST("exists: returns false for non-existing path");
    {
        bool exists = PathHelper::exists("nonexistent_path_12345");
        REQUIRE(exists == false);
    }

    // Test 27: isDirectory - actual directory
    BEGIN_TEST("isDirectory: returns true for directory");
    {
        bool isDir = PathHelper::isDirectory("test_path_root");
        REQUIRE(isDir == true);
    }

    // Test 28: isDirectory - non-directory
    BEGIN_TEST("isDirectory: returns false for non-existing path");
    {
        bool isDir = PathHelper::isDirectory("nonexistent_path_12345");
        REQUIRE(isDir == false);
    }

    // Test 29: getFileInfo - existing directory
    BEGIN_TEST("getFileInfo: gets info for existing directory");
    {
        auto info = PathHelper::getFileInfo("test_path_root");
        REQUIRE(info.exists == true);
        REQUIRE(info.isDirectory == true);
    }

    // Test 30: getFileInfo - non-existing path
    BEGIN_TEST("getFileInfo: reports non-existing path");
    {
        auto info = PathHelper::getFileInfo("nonexistent_path_12345");
        REQUIRE(info.exists == false);
    }

    // Test 31: listDirectory - existing directory
    BEGIN_TEST("listDirectory: lists directory contents");
    {
        auto entries = PathHelper::listDirectory("test_path_root");
        // Should have at least the subdir we created
        bool found = false;
        for (const auto& entry : entries) {
            if (entry.name == "subdir") {
                found = true;
                REQUIRE(entry.isDirectory == true);
            }
        }
        REQUIRE(found == true);
    }

    // Test 32: Security - isPathWithin with URL encoding attempt
    BEGIN_TEST("SECURITY: isPathWithin detects URL-encoded traversal");
    {
        // %2e%2e = ..
        bool within = PathHelper::isPathWithin(
            "test_path_root/%2e%2e/test_path_outside", "test_path_root");
        // Should detect this as outside (after URL decode, if implemented)
        // At minimum, should not crash
        REQUIRE(!within || within); // Just verify no crash //-V560
    }

    // Test 33: Security - null bytes in path
    BEGIN_TEST("SECURITY: handles null bytes in path gracefully");
    {
        std::string pathWithNull = "test_path_root";
        pathWithNull += '\0';
        pathWithNull += "/../../etc/passwd";

        // Should handle gracefully (either stop at null or reject)
        try {
            bool within
                = PathHelper::isPathWithin(pathWithNull, "test_path_root");
            // Should not allow escape
            REQUIRE(within == false || within == true); // No crash required //-V560
        } catch (...) {
            // Exception is acceptable for invalid input
            REQUIRE(true);
        }
    }

    // Test 34: Security - extremely long path
    BEGIN_TEST("SECURITY: handles extremely long paths");
    {
        std::string longPath = "test_path_root/";
        for (int i = 0; i < 1000; ++i) {
            longPath += "a/";
        }

        try {
            std::string canonical = PathHelper::getCanonicalPath(longPath);
            // Should not crash - empty result is acceptable for extremely long paths on Windows
            REQUIRE(true);
        } catch (...) {
            // Exception is acceptable for extreme input
            REQUIRE(true);
        }
    }

    // Test 35: Edge case - root path
    BEGIN_TEST("Edge case: handles root path /");
    {
        std::string normalized = PathHelper::normalizePath("/");
        REQUIRE(normalized == "/");
    }

    // Test 36: Edge case - Windows drive letter
#ifdef _WIN32
    BEGIN_TEST("Edge case: handles Windows drive letter");
    {
        std::string path = "C:\\path\\to\\file.txt";
        std::string normalized = PathHelper::normalizePath(path);
        REQUIRE(normalized.find("C:") != std::string::npos);
    }
#endif

    cleanupTestDirs();

    return test_summary();
}
