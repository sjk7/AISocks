// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include <string>
#include <vector>
#include <map>
#include <ctime>

namespace aiSocks {

/// Simple header-only file cache for serving static content
/// Caches file content in memory and invalidates when file modification time changes
class FileCache {
public:
    struct CachedFile {
        std::vector<char> content;
        time_t lastModified = 0;
        size_t size = 0;
    };

    FileCache() = default;
    
    /// Check if file is in cache and still valid
    /// Returns nullptr if not cached or cache is stale
    const CachedFile* get(const std::string& filePath, time_t currentModTime) const {
        auto it = cache_.find(filePath);
        if (it == cache_.end()) {
            return nullptr; // Not in cache
        }
        
        // Check if cache is stale (file was modified)
        if (it->second.lastModified != currentModTime) {
            return nullptr; // Cache is stale
        }
        
        return &it->second;
    }
    
    /// Add or update file in cache
    void put(const std::string& filePath, const std::vector<char>& content, time_t modTime) {
        CachedFile cached;
        cached.content = content;
        cached.lastModified = modTime;
        cached.size = content.size();
        
        cache_[filePath] = cached;
    }
    
    /// Remove file from cache
    void invalidate(const std::string& filePath) {
        cache_.erase(filePath);
    }
    
    /// Clear entire cache
    void clear() {
        cache_.clear();
    }
    
    /// Get cache statistics
    size_t size() const {
        return cache_.size();
    }
    
    /// Get total bytes cached
    size_t totalBytes() const {
        size_t total = 0;
        for (const auto& [path, cached] : cache_) {
            (void)path; // Suppress unused warning
            total += cached.size;
        }
        return total;
    }

private:
    std::map<std::string, CachedFile> cache_;
};

} // namespace aiSocks
