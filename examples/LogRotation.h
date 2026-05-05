// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#pragma once

#include <cstddef>
#include <string>
#include <functional>

namespace aiSocks {

// ---------------------------------------------------------------------------
// LogRotation - Handles size-based log file rotation
// ---------------------------------------------------------------------------
class LogRotation {
    public:
    // Callback type: called after a file is rotated, receives the full path of the rotated file
    using RotationCallback = std::function<void(const std::string& rotatedFilePath)>;
    
    struct Config {
        size_t maxSizeBytes; // 10MB default
        size_t maxFiles; // Keep 5 rotated files
        bool enabled;
        
        Config() : maxSizeBytes(10 * 1024 * 1024), maxFiles(5), enabled(true) {}
    };

    explicit LogRotation(const std::string& basePath, const Config& config = Config{});
    
    // Check if rotation is needed based on current file size
    bool shouldRotate() const;
    
    // Perform rotation: moves current log to .1, .1 to .2, etc.
    // Returns true on success, false on error
    bool rotate();
    
    // Set a callback to be invoked after a file is rotated
    void setCallback(RotationCallback callback) { rotationCallback_ = std::move(callback); }
    
    // Get the current log file path
    const std::string& getLogPath() const { return logPath_; }
    
    // Get rotation config
    const Config& getConfig() const { return config_; }
    
    private:
    std::string logPath_;
    Config config_;
    RotationCallback rotationCallback_;
    
    // Get the path for a rotated log file (e.g., access.log.1)
    std::string getRotatedPath(int index) const;
    
    // Remove a rotated log file
    bool removeRotatedFile(int index);
    
    // Rename/move a file
    bool renameFile(const std::string& from, const std::string& to);
};

} // namespace aiSocks
