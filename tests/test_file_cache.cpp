// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com



#include "FileCache.h"
#include "test_helpers.h"
#include <iostream>
#include <string>
#include <vector>

using namespace aiSocks;

int main() {
    std::cout << "=== FileCache Tests ===\n";

    // Test 1: Basic cache operations
    BEGIN_TEST("FileCache: put and get");
    {
        FileCache cache;
        REQUIRE(cache.size() == 0);
        REQUIRE(cache.totalBytes() == 0);
        
        std::vector<char> content = {'H', 'e', 'l', 'l', 'o'};
        time_t modTime = 12345;
        
        cache.put("/test/file.txt", content, modTime);
        REQUIRE(cache.size() == 1);
        REQUIRE(cache.totalBytes() == 5);
        
        const FileCache::CachedFile* cached = cache.get("/test/file.txt", modTime);
        REQUIRE(cached != nullptr);
        REQUIRE(cached->size == 5);
        REQUIRE(cached->lastModified == modTime);
        REQUIRE(cached->content == content);
    }

    // Test 2: Cache miss on wrong modification time
    BEGIN_TEST("FileCache: cache invalidation on mod time change");
    {
        FileCache cache;
        std::vector<char> content = {'T', 'e', 's', 't'};
        time_t oldTime = 1000;
        time_t newTime = 2000;
        
        cache.put("/file.txt", content, oldTime);
        
        // Get with same time - should hit
        const FileCache::CachedFile* cached = cache.get("/file.txt", oldTime);
        REQUIRE(cached != nullptr);
        
        // Get with different time - should miss (stale cache)
        cached = cache.get("/file.txt", newTime);
        REQUIRE(cached == nullptr);
    }

    // Test 3: Cache miss on non-existent file
    BEGIN_TEST("FileCache: cache miss on non-existent file");
    {
        FileCache cache;
        const FileCache::CachedFile* cached = cache.get("/nonexistent.txt", 0);
        REQUIRE(cached == nullptr);
    }

    // Test 4: Multiple files in cache
    BEGIN_TEST("FileCache: multiple files");
    {
        FileCache cache;
        
        std::vector<char> content1 = {'F', 'i', 'l', 'e', '1'};
        std::vector<char> content2 = {'F', 'i', 'l', 'e', '2', '2'};
        std::vector<char> content3 = {'F', 'i', 'l', 'e', '3', '3', '3'};
        
        cache.put("/file1.txt", content1, 100);
        cache.put("/file2.txt", content2, 200);
        cache.put("/file3.txt", content3, 300);
        
        REQUIRE(cache.size() == 3);
        REQUIRE(cache.totalBytes() == 5 + 6 + 7);
        
        const FileCache::CachedFile* c1 = cache.get("/file1.txt", 100);
        const FileCache::CachedFile* c2 = cache.get("/file2.txt", 200);
        const FileCache::CachedFile* c3 = cache.get("/file3.txt", 300);
        
        REQUIRE(c1 != nullptr && c1->size == 5);
        REQUIRE(c2 != nullptr && c2->size == 6);
        REQUIRE(c3 != nullptr && c3->size == 7);
    }

    // Test 5: Cache update (overwrite)
    BEGIN_TEST("FileCache: update existing entry");
    {
        FileCache cache;
        
        std::vector<char> oldContent = {'O', 'l', 'd'};
        std::vector<char> newContent = {'N', 'e', 'w', 'e', 'r'};
        
        cache.put("/file.txt", oldContent, 100);
        REQUIRE(cache.size() == 1);
        REQUIRE(cache.totalBytes() == 3);
        
        // Update with new content
        cache.put("/file.txt", newContent, 200);
        REQUIRE(cache.size() == 1); // Still one entry
        REQUIRE(cache.totalBytes() == 5); // Updated size
        
        const FileCache::CachedFile* cached = cache.get("/file.txt", 200);
        REQUIRE(cached != nullptr);
        REQUIRE(cached->content == newContent);
    }

    // Test 6: Cache invalidate
    BEGIN_TEST("FileCache: invalidate specific file");
    {
        FileCache cache;
        
        std::vector<char> content1 = {'A'};
        std::vector<char> content2 = {'B'};
        
        cache.put("/file1.txt", content1, 100);
        cache.put("/file2.txt", content2, 200);
        REQUIRE(cache.size() == 2);
        
        cache.invalidate("/file1.txt");
        REQUIRE(cache.size() == 1);
        
        const FileCache::CachedFile* c1 = cache.get("/file1.txt", 100);
        const FileCache::CachedFile* c2 = cache.get("/file2.txt", 200);
        
        REQUIRE(c1 == nullptr);
        REQUIRE(c2 != nullptr);
    }

    // Test 7: Cache clear
    BEGIN_TEST("FileCache: clear all entries");
    {
        FileCache cache;
        
        std::vector<char> content = {'X'};
        cache.put("/file1.txt", content, 100);
        cache.put("/file2.txt", content, 200);
        cache.put("/file3.txt", content, 300);
        
        REQUIRE(cache.size() == 3);
        
        cache.clear();
        REQUIRE(cache.size() == 0);
        REQUIRE(cache.totalBytes() == 0);
        
        const FileCache::CachedFile* cached = cache.get("/file1.txt", 100);
        REQUIRE(cached == nullptr);
    }

    // Test 8: Large content
    BEGIN_TEST("FileCache: large file content");
    {
        FileCache cache;
        
        std::vector<char> largeContent(10000, 'X');
        cache.put("/large.bin", largeContent, 500);
        
        REQUIRE(cache.size() == 1);
        REQUIRE(cache.totalBytes() == 10000);
        
        const FileCache::CachedFile* cached = cache.get("/large.bin", 500);
        REQUIRE(cached != nullptr);
        REQUIRE(cached->size == 10000);
        REQUIRE(cached->content.size() == 10000);
    }

    // Test 9: Empty content
    BEGIN_TEST("FileCache: empty file content");
    {
        FileCache cache;
        
        std::vector<char> emptyContent;
        cache.put("/empty.txt", emptyContent, 100);
        
        REQUIRE(cache.size() == 1);
        REQUIRE(cache.totalBytes() == 0);
        
        const FileCache::CachedFile* cached = cache.get("/empty.txt", 100);
        REQUIRE(cached != nullptr);
        REQUIRE(cached->size == 0);
        REQUIRE(cached->content.empty());
    }

    // Test 10: Path variations
    BEGIN_TEST("FileCache: different path formats");
    {
        FileCache cache;
        std::vector<char> content = {'T'};
        
        cache.put("/path/to/file.txt", content, 100);
        cache.put("relative/path.txt", content, 200);
        cache.put("C:\\Windows\\file.txt", content, 300);
        
        REQUIRE(cache.size() == 3);
        
        REQUIRE(cache.get("/path/to/file.txt", 100) != nullptr);
        REQUIRE(cache.get("relative/path.txt", 200) != nullptr);
        REQUIRE(cache.get("C:\\Windows\\file.txt", 300) != nullptr);
    }

    std::cout << "\n=== All FileCache tests passed ===\n";
    return 0;
}
