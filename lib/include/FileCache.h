// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include <string>
#include <vector>
#include <map>
#include <list>
#include <ctime>

namespace aiSocks {

/// Simple header-only file cache for serving static content
/// Caches file content in memory and invalidates when file modification time changes
/// Uses LRU eviction policy with configurable size and memory limits
class FileCache {
public:
    struct CachedFile {
        std::vector<char> content;
        time_t lastModified = 0;
        size_t size = 0;
    };
    
    struct Config {
        static constexpr size_t DEFAULT_MAX_ENTRIES = 100;
        static constexpr size_t DEFAULT_MAX_TOTAL_BYTES = 50 * 1024 * 1024;  // 50MB
        static constexpr size_t DEFAULT_MAX_FILE_SIZE = 5 * 1024 * 1024;     // 5MB
        
        size_t maxEntries = DEFAULT_MAX_ENTRIES;
        size_t maxTotalBytes = DEFAULT_MAX_TOTAL_BYTES;
        size_t maxFileSize = DEFAULT_MAX_FILE_SIZE;
    };

    FileCache() = default;
    
    explicit FileCache(const Config& config) : config_(config) {}
    
    /// Check if file is in cache and still valid
    /// Returns nullptr if not cached or cache is stale
    const CachedFile* get(const std::string& filePath, time_t currentModTime) {
        auto it = cache_.find(filePath);
        if (it == cache_.end()) {
            return nullptr;
        }
        
        if (it->second.lastModified != currentModTime) {
            totalBytes_ -= it->second.size;
            cache_.erase(it);
            removeLRUEntry(filePath);
            return nullptr;
        }
        
        updateLRU(filePath);
        
        return &it->second;
    }
    
    /// Add or update file in cache
    void put(const std::string& filePath, const std::vector<char>& content, time_t modTime) {
        if (content.size() > config_.maxFileSize) {
            return;
        }
        
        auto it = cache_.find(filePath);
        if (it != cache_.end()) {
            totalBytes_ -= it->second.size;
            cache_.erase(it);
            removeLRUEntry(filePath);
        }
        
        while (!lruList_.empty() && 
               (cache_.size() >= config_.maxEntries || 
                totalBytes_ + content.size() > config_.maxTotalBytes)) {
            evictLRU();
        }
        
        CachedFile cached;
        cached.content = content;
        cached.lastModified = modTime;
        cached.size = content.size();
        
        cache_[filePath] = cached;
        totalBytes_ += content.size();
        
        lruList_.push_front(filePath);
    }
    
    /// Remove file from cache
    void invalidate(const std::string& filePath) {
        auto it = cache_.find(filePath);
        if (it != cache_.end()) {
            totalBytes_ -= it->second.size;
            cache_.erase(it);
            removeLRUEntry(filePath);
        }
    }
    
    /// Clear entire cache
    void clear() {
        cache_.clear();
        lruList_.clear();
        totalBytes_ = 0;
    }
    
    /// Get cache statistics
    size_t size() const {
        return cache_.size();
    }
    
    /// Get total bytes cached
    size_t totalBytes() const {
        return totalBytes_;
    }
    
    /// Get cache configuration
    const Config& getConfig() const {
        return config_;
    }

private:
    Config config_;
    std::map<std::string, CachedFile> cache_;
    std::list<std::string> lruList_;
    size_t totalBytes_ = 0;
    
    void evictLRU() {
        if (lruList_.empty()) return;
        
        std::string lruPath = lruList_.back();
        lruList_.pop_back();
        
        auto it = cache_.find(lruPath);
        if (it != cache_.end()) {
            totalBytes_ -= it->second.size;
            cache_.erase(it);
        }
    }
    
    void updateLRU(const std::string& filePath) {
        removeLRUEntry(filePath);
        lruList_.push_front(filePath);
    }
    
    void removeLRUEntry(const std::string& filePath) {
        for (auto it = lruList_.begin(); it != lruList_.end(); ++it) {
            if (*it == filePath) {
                lruList_.erase(it);
                break;
            }
        }
    }
};

} // namespace aiSocks
