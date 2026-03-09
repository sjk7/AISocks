// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "FileCache.h"

namespace aiSocks {

FileCache::FileCache(const Config& config) : config_(config) {}

const FileCache::CachedFile* FileCache::get(
    const std::string& filePath, time_t currentModTime) {
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

void FileCache::put(const std::string& filePath,
    const std::vector<char>& content, time_t modTime) {
    putImpl(filePath, content, modTime);
}

void FileCache::put(
    const std::string& filePath, std::vector<char>&& content, time_t modTime) {
    putImpl(filePath, std::move(content), modTime);
}

void FileCache::putImpl(
    const std::string& filePath, std::vector<char> content, time_t modTime) {
    if (content.size() > config_.maxFileSize) {
        return;
    }

    auto it = cache_.find(filePath);
    if (it != cache_.end()) {
        totalBytes_ -= it->second.size;
        cache_.erase(it);
        removeLRUEntry(filePath);
    }

    while (!lruList_.empty()
        && (cache_.size() >= config_.maxEntries
            || totalBytes_ + content.size() > config_.maxTotalBytes)) {
        evictLRU();
    }

    const size_t sz = content.size();
    CachedFile cached;
    cached.content = std::move(content);
    cached.lastModified = modTime;
    cached.size = sz;

    cache_[filePath] = std::move(cached);
    totalBytes_ += sz;

    lruList_.push_front(filePath);
    lruIndex_[filePath] = lruList_.begin();
}

void FileCache::invalidate(const std::string& filePath) {
    auto it = cache_.find(filePath);
    if (it != cache_.end()) {
        totalBytes_ -= it->second.size;
        cache_.erase(it);
        removeLRUEntry(filePath);
    }
}

void FileCache::clear() {
    cache_.clear();
    lruList_.clear();
    lruIndex_.clear();
    totalBytes_ = 0;
}

size_t FileCache::size() const {
    return cache_.size();
}

size_t FileCache::totalBytes() const {
    return totalBytes_;
}

const FileCache::Config& FileCache::getConfig() const {
    return config_;
}

void FileCache::evictLRU() {
    if (lruList_.empty()) return;

    const std::string& lruPath = lruList_.back();
    lruIndex_.erase(lruPath);

    auto it = cache_.find(lruPath);
    if (it != cache_.end()) {
        totalBytes_ -= it->second.size;
        cache_.erase(it);
    }

    lruList_.pop_back();
}

void FileCache::updateLRU(const std::string& filePath) {
    removeLRUEntry(filePath);
    lruList_.push_front(filePath);
    lruIndex_[filePath] = lruList_.begin();
}

void FileCache::removeLRUEntry(const std::string& filePath) {
    auto idxIt = lruIndex_.find(filePath);
    if (idxIt != lruIndex_.end()) {
        lruList_.erase(idxIt->second);
        lruIndex_.erase(idxIt);
    }
}

} // namespace aiSocks
