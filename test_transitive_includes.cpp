// Test to identify which standard library headers pull in <format> and <system_error>
// Compile with: cl /P /Fi"output.i" /std:c++20 test_transitive_includes.cpp
// Then search output.i for "format" and "system_error"

// Test 1: chrono
#include <chrono>
#ifdef __has_include
#  if __has_include(<system_error>)
// chrono includes system_error
#  endif
#  if __has_include(<format>)
// chrono includes format
#  endif
#endif

// Test 2: string
#include <string>

// Test 3: exception
#include <exception>

// Test 4: stdexcept
#include <stdexcept>

int main() {
    // Check if system_error types are available without explicit include
    #ifdef _SYSTEM_ERROR_
    #pragma message("system_error was included transitively")
    #endif
    
    // Check if format is available without explicit include
    #ifdef _FORMAT_
    #pragma message("format was included transitively")
    #endif
    
    return 0;
}
