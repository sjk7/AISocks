// This is an independent project of an individual developer. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com



#include "FileIO.h"
#include "test_helpers.h"
#include <iostream>
#include <string>

#ifdef _WIN32
    #include <direct.h>
    #define mkdir(path, mode) _mkdir(path)
#else
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

using namespace aiSocks;

int main() {
    std::cout << "=== FileIO Tests ===\n";

    // Test 1: File creation and writing
    BEGIN_TEST("File: create and write");
    {
        File file("test_file_io.txt", "w");
        REQUIRE(file.isOpen());
        REQUIRE(file.writeString("Hello, World!\n"));
        REQUIRE(file.writeString(std::string("Line 2\n")));
        file.close();
        REQUIRE(!file.isOpen());
    }

    // Test 2: File reading
    BEGIN_TEST("File: read content");
    {
        File file("test_file_io.txt", "r");
        REQUIRE(file.isOpen());
        
        std::vector<char> content = file.readAll();
        REQUIRE(!content.empty());
        
        std::string str(content.begin(), content.end());
        REQUIRE(str.find("Hello, World!") != std::string::npos);
        REQUIRE(str.find("Line 2") != std::string::npos);
    }

    // Test 3: File size
    BEGIN_TEST("File: get size");
    {
        File file("test_file_io.txt", "r");
        REQUIRE(file.isOpen());
        
        size_t sz = file.size();
        REQUIRE(sz > 0);
        REQUIRE(sz == 21); // "Hello, World!\nLine 2\n"
    }

    // Test 4: File printf
    BEGIN_TEST("File: printf formatting");
    {
        File file("test_printf.txt", "w");
        REQUIRE(file.isOpen());
        REQUIRE(file.printf("Number: %d, String: %s\n", 42, "test"));
        file.close();
        
        File readFile("test_printf.txt", "r");
        std::vector<char> content = readFile.readAll();
        std::string str(content.begin(), content.end());
        REQUIRE(str.find("Number: 42") != std::string::npos);
        REQUIRE(str.find("String: test") != std::string::npos);
    }

    // Test 5: File move semantics
    BEGIN_TEST("File: move constructor");
    {
        File file1("test_move.txt", "w");
        REQUIRE(file1.isOpen());
        file1.writeString("Move test");
        
        File file2(std::move(file1));
        REQUIRE(file2.isOpen());
        REQUIRE(!file1.isOpen());
        
        file2.close();
    }

    // Test 6: File move assignment
    BEGIN_TEST("File: move assignment");
    {
        File file1("test_move2.txt", "w");
        REQUIRE(file1.isOpen());
        
        File file2;
        REQUIRE(!file2.isOpen());
        
        file2 = std::move(file1);
        REQUIRE(file2.isOpen());
        REQUIRE(!file1.isOpen());
    }

    // Test 7: File locking (shared read, exclusive write)
    BEGIN_TEST("File: shared read locks, exclusive write locks");
    {
        // Write mode gets exclusive lock
        File file1("test_lock.txt", "w");
        REQUIRE(file1.isOpen());
        file1.writeString("Locked file");
        file1.flush();
        
        // Try to open for write while write-locked - should fail
        File file2("test_lock.txt", "w");
        REQUIRE(!file2.isOpen()); // Exclusive lock prevents second write
        
        file1.close();
        
        // Multiple readers should be allowed (shared locks)
        File file3("test_lock.txt", "r");
        REQUIRE(file3.isOpen());
        
#ifndef _WIN32
        // On Unix, shared locks allow concurrent reads
        File file4("test_lock.txt", "r");
        REQUIRE(file4.isOpen());
        file4.close();
#endif
        // On Windows, we don't lock for read-only mode to allow concurrent reads
        
        file3.close();
        
        // Write lock should prevent reads on Unix (with shared locking)
        File file5("test_lock.txt", "w");
        REQUIRE(file5.isOpen());
        
#ifndef _WIN32
        File file6("test_lock.txt", "r");
        REQUIRE(!file6.isOpen()); // Write lock prevents read on Unix
#endif
    }

    // Test 8: getInfoFromDescriptor
    BEGIN_TEST("File: getInfoFromDescriptor");
    {
        File file("test_file_io.txt", "r");
        REQUIRE(file.isOpen());
        
        File::FileInfo info = file.getInfoFromDescriptor();
        REQUIRE(info.valid);
        REQUIRE(info.isRegular);
        REQUIRE(!info.isDirectory);
        REQUIRE(!info.isSymlink);
        REQUIRE(info.size > 0);
        REQUIRE(info.lastModified > 0);
    }

    // Test 9: StringBuilder basic operations
    BEGIN_TEST("StringBuilder: append and toString");
    {
        StringBuilder sb;
        REQUIRE(sb.empty());
        REQUIRE(sb.size() == 0);
        
        sb.append("Hello");
        REQUIRE(!sb.empty());
        REQUIRE(sb.size() == 5);
        
        sb.append(" ");
        sb.append("World");
        REQUIRE(sb.size() == 11);
        
        std::string result = sb.toString();
        REQUIRE(result == "Hello World");
    }

    // Test 10: StringBuilder appendFormat
    BEGIN_TEST("StringBuilder: appendFormat");
    {
        StringBuilder sb;
        sb.append("Count: ");
        REQUIRE(sb.appendFormat("%d", 42));
        sb.append(", Name: ");
        REQUIRE(sb.appendFormat("%s", "Test"));
        
        std::string result = sb.toString();
        REQUIRE(result == "Count: 42, Name: Test");
    }

    // Test 11: StringBuilder reserve and capacity
    BEGIN_TEST("StringBuilder: reserve capacity");
    {
        StringBuilder sb(100);
        sb.append("Small text");
        REQUIRE(sb.size() == 10);
        
        // Should not reallocate for small appends
        sb.append(" more text");
        REQUIRE(sb.toString() == "Small text more text");
    }

    // Test 12: StringBuilder clear
    BEGIN_TEST("StringBuilder: clear");
    {
        StringBuilder sb;
        sb.append("Some content");
        REQUIRE(!sb.empty());
        
        sb.clear();
        REQUIRE(sb.empty());
        REQUIRE(sb.size() == 0);
        
        sb.append("New content");
        REQUIRE(sb.toString() == "New content");
    }

    // Cleanup
    remove("test_file_io.txt");
    remove("test_printf.txt");
    remove("test_move.txt");
    remove("test_move2.txt");
    remove("test_lock.txt");

    std::cout << "\n=== All FileIO tests passed ===\n";
    return 0;
}
