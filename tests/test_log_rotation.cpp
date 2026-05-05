// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// https://pvs-studio.com

#include "LogRotation.h"
#include "FileIO.h"
#include <cstdio>
#include <cstring>
#include <string>

#define BEGIN_TEST(name) printf("\n=== %s ===\n", name)
#define REQUIRE(cond) \
    do { \
        if (!(cond)) { \
            printf("FAILED: %s at line %d\n", #cond, __LINE__); \
            return false; \
        } \
    } while(0)

namespace aiSocks {

bool test_log_rotation_basic() {
    BEGIN_TEST("LogRotation: basic rotation");
    
    // Clean up any existing test files
    std::remove("test_rotation.log");
    std::remove("test_rotation.log.1");
    std::remove("test_rotation.log.2");
    std::remove("test_rotation.log.3");
    
    // Create initial log file
    {
        File f("test_rotation.log", "w");
        REQUIRE(f.isOpen());
        REQUIRE(f.writeString("Initial log content\n"));
    }
    
    // Create LogRotation with small max size (100 bytes)
    LogRotation::Config config;
    config.maxSizeBytes = 100;
    config.maxFiles = 3;
    config.enabled = true;
    
    LogRotation rotator("test_rotation.log", config);
    
    // Check if rotation is needed (file is small, should not rotate)
    REQUIRE(!rotator.shouldRotate());
    
    // Write more data to exceed max size
    {
        File f("test_rotation.log", "a");
        REQUIRE(f.isOpen());
        std::string largeData(200, 'X');
        REQUIRE(f.writeString(largeData));
    }
    
    // Now rotation should be needed
    REQUIRE(rotator.shouldRotate());
    
    // Perform rotation
    REQUIRE(rotator.rotate());
    
    // Check that .1 file exists
    {
        File f("test_rotation.log.1", "r");
        REQUIRE(f.isOpen());
    }
    
    // Create new log file (simulating what the server does after rotation)
    {
        File f("test_rotation.log", "w");
        REQUIRE(f.isOpen());
    }
    
    // Cleanup
    std::remove("test_rotation.log");
    std::remove("test_rotation.log.1");
    std::remove("test_rotation.log.2");
    std::remove("test_rotation.log.3");
    
    return true;
}

bool test_log_rotation_disabled() {
    BEGIN_TEST("LogRotation: disabled rotation");
    
    // Clean up
    std::remove("test_rotation.log");
    
    // Create log file
    {
        File f("test_rotation.log", "w");
        REQUIRE(f.writeString("Content\n"));
    }
    
    // Create LogRotation with rotation disabled
    LogRotation::Config config;
    config.enabled = false;
    
    LogRotation rotator("test_rotation.log", config);
    
    // Should never rotate when disabled
    REQUIRE(!rotator.shouldRotate());
    
    // Cleanup
    std::remove("test_rotation.log");
    
    return true;
}

bool test_log_rotation_max_files() {
    BEGIN_TEST("LogRotation: max files limit");
    
    // Clean up
    std::remove("test_rotation.log");
    std::remove("test_rotation.log.1");
    std::remove("test_rotation.log.2");
    std::remove("test_rotation.log.3");
    std::remove("test_rotation.log.4");
    std::remove("test_rotation.log.5");
    
    // Create LogRotation with maxFiles = 2
    LogRotation::Config config;
    config.maxSizeBytes = 10;
    config.maxFiles = 2;
    config.enabled = true;
    
    LogRotation rotator("test_rotation.log", config);
    
    // Create initial log
    {
        File f("test_rotation.log", "w");
        REQUIRE(f.writeString("Data\n"));
    }
    
    // Rotate multiple times
    for (int i = 0; i < 4; ++i) {
        {
            File f("test_rotation.log", "a");
            REQUIRE(f.writeString(std::string(20, 'X')));
        }
        REQUIRE(rotator.shouldRotate());
        REQUIRE(rotator.rotate());
    }
    
    // Check that only .1 and .2 exist (maxFiles = 2)
    {
        File f1("test_rotation.log.1", "r");
        File f2("test_rotation.log.2", "r");
        REQUIRE(f1.isOpen());
        REQUIRE(f2.isOpen());
    }
    
    // .3 should not exist (beyond maxFiles)
    {
        File f3("test_rotation.log.3", "r");
        REQUIRE(!f3.isOpen());
    }
    
    // Cleanup
    std::remove("test_rotation.log");
    std::remove("test_rotation.log.1");
    std::remove("test_rotation.log.2");
    std::remove("test_rotation.log.3");
    std::remove("test_rotation.log.4");
    std::remove("test_rotation.log.5");
    
    return true;
}

bool test_log_rotation_config_defaults() {
    BEGIN_TEST("LogRotation: config defaults");
    
    LogRotation::Config config;
    
    REQUIRE(config.maxSizeBytes == 10 * 1024 * 1024); // 10MB
    REQUIRE(config.maxFiles == 5);
    REQUIRE(config.enabled == true);
    
    return true;
}

bool test_log_rotation_empty_file() {
    BEGIN_TEST("LogRotation: empty file handling");
    
    // Clean up
    std::remove("test_rotation.log");
    
    // Create empty log file
    {
        File f("test_rotation.log", "w");
        REQUIRE(f.isOpen());
    }
    
    LogRotation::Config config;
    config.maxSizeBytes = 100;
    config.enabled = true;
    
    LogRotation rotator("test_rotation.log", config);
    
    // Empty file should not trigger rotation
    REQUIRE(!rotator.shouldRotate());
    
    // Cleanup
    std::remove("test_rotation.log");
    
    return true;
}

bool test_log_rotation_callback() {
    BEGIN_TEST("LogRotation: callback invocation");
    
    // Clean up
    std::remove("test_rotation.log");
    std::remove("test_rotation.log.1");
    
    // Create log file
    {
        File f("test_rotation.log", "w");
        REQUIRE(f.writeString("Initial\n"));
    }
    
    LogRotation::Config config;
    config.maxSizeBytes = 100;
    config.enabled = true;
    
    LogRotation rotator("test_rotation.log", config);
    
    // Set callback to capture the rotated file path
    std::string callbackPath;
    rotator.setCallback([&callbackPath](const std::string& path) {
        callbackPath = path;
    });
    
    // Write data to trigger rotation
    {
        File f("test_rotation.log", "a");
        REQUIRE(f.writeString(std::string(200, 'X')));
    }
    
    REQUIRE(rotator.shouldRotate());
    REQUIRE(rotator.rotate());
    
    // Check that callback was invoked with correct path
    REQUIRE(callbackPath == "test_rotation.log.1");
    
    // Cleanup
    std::remove("test_rotation.log");
    std::remove("test_rotation.log.1");
    
    return true;
}

} // namespace aiSocks

int main() {
    printf("=== LogRotation Tests ===\n");
    
    bool allPassed = true;
    
    allPassed &= aiSocks::test_log_rotation_config_defaults();
    allPassed &= aiSocks::test_log_rotation_disabled();
    allPassed &= aiSocks::test_log_rotation_empty_file();
    allPassed &= aiSocks::test_log_rotation_basic();
    allPassed &= aiSocks::test_log_rotation_max_files();
    allPassed &= aiSocks::test_log_rotation_callback();
    
    printf("\n=== Test Summary ===\n");
    if (allPassed) {
        printf("All tests PASSED\n");
        return 0;
    } else {
        printf("Some tests FAILED\n");
        return 1;
    }
}
