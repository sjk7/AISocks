// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include <ctime>
#include <list>
#include <map>
#include <string>
#include <vector>

namespace aiSocks {

/// Simple file cache for serving static content.
/// Caches file content in memory and invalidates when file modification time
/// changes. Uses LRU eviction policy with configurable size and memory limits.
class FileCache {
    public:
    struct CachedFile {
        std::vector<char> content;
        time_t lastModified = 0;
        size_t size = 0;
    };

    struct Config {
        static constexpr size_t DEFAULT_MAX_ENTRIES = 100;
        static constexpr size_t DEFAULT_MAX_TOTAL_BYTES
            = 50 * 1024 * 1024; // 50MB
        static constexpr size_t DEFAULT_MAX_FILE_SIZE = 5 * 1024 * 1024; // 5MB

        size_t maxEntries = DEFAULT_MAX_ENTRIES;
        size_t maxTotalBytes = DEFAULT_MAX_TOTAL_BYTES;
        size_t maxFileSize = DEFAULT_MAX_FILE_SIZE;
    };

    FileCache() = default;
    explicit FileCache(const Config& config);

    /// Returns nullptr if not cached or cache is stale.
    const CachedFile* get(const std::string& filePath, time_t currentModTime);

    void put(const std::string& filePath, const std::vector<char>& content,
        time_t modTime);

    void invalidate(const std::string& filePath);
    void clear();

    size_t size() const;
    size_t totalBytes() const;
    const Config& getConfig() const;

    private:
    Config config_;
    std::map<std::string, CachedFile> cache_;
    std::list<std::string> lruList_;
    size_t totalBytes_ = 0;

    void evictLRU();
    void updateLRU(const std::string& filePath);
    void removeLRUEntry(const std::string& filePath);
};

} // namespace aiSocks
