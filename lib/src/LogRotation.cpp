// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "LogRotation.h"
#include "FileIO.h"
#include <cstdio>
#include <cstring>

namespace aiSocks {

LogRotation::LogRotation(const std::string& basePath, const Config& config)
    : logPath_(basePath), config_(config) {
}

bool LogRotation::shouldRotate() const {
    if (!config_.enabled) return false;
    
    File file(logPath_.c_str(), "rb");
    if (!file.isOpen()) return false;
    
    // Get file size
    if (std::fseek(file.get(), 0, SEEK_END) != 0) return false;
    long size = std::ftell(file.get());
    if (size < 0) return false;
    
    return static_cast<size_t>(size) >= config_.maxSizeBytes;
}

bool LogRotation::rotate() {
    if (!config_.enabled) return false;
    
    // Rotate files in reverse order: .N -> .N+1
    for (int i = static_cast<int>(config_.maxFiles) - 1; i >= 1; --i) {
        std::string from = getRotatedPath(i);
        std::string to = getRotatedPath(i + 1);
        
        // Remove the destination if it exists
        removeRotatedFile(i + 1);
        
        // Move the file
        if (!renameFile(from, to)) {
            // If rename fails, try to remove the source
            removeRotatedFile(i);
        }
    }
    
    // Move current log to .1
    std::string to = getRotatedPath(1);
    removeRotatedFile(1);
    if (!renameFile(logPath_, to)) {
        return false;
    }
    
    // Invoke callback with the rotated file path
    if (rotationCallback_) {
        rotationCallback_(to);
    }
    
    return true;
}

std::string LogRotation::getRotatedPath(int index) const {
    return logPath_ + "." + std::to_string(index);
}

bool LogRotation::removeRotatedFile(int index) {
    std::string path = getRotatedPath(index);
#ifdef _MSC_VER
    return _remove(path.c_str()) == 0;
#else
    return std::remove(path.c_str()) == 0;
#endif
}

bool LogRotation::renameFile(const std::string& from, const std::string& to) {
#ifdef _MSC_VER
    return _rename(from.c_str(), to.c_str()) == 0;
#else
    return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

} // namespace aiSocks
