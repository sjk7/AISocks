# Memory and Address Sanitizer Setup

This guide explains how to use MemorySanitizer (MSan) and AddressSanitizer (ASan) with aiSocks in Windsurf/VS Code.

## Quick Start

### MemorySanitizer (MSan) - Debug Mode

1. **Configure the build:**
   ```bash
   ./setup_msan.sh
   ```

2. **Build the project:**
   ```bash
   cmake --build build-msan --target aiSocksExample
   ```

3. **Debug in Windsurf:**
   - Press `F5` or go to Run and Debug
   - Select "Debug aiSocksExample (MemorySanitizer)"
   - The debugger will launch with MSan enabled

### AddressSanitizer (ASan) - Debug Mode

1. **Configure the build:**
   ```bash
   mkdir -p build-asan
   cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -G Ninja
   ```

2. **Build the project:**
   ```bash
   cmake --build build-asan --target aiSocksExample
   ```

3. **Debug in Windsurf:**
   - Press `F5` or go to Run and Debug
   - Select "Debug aiSocksExample (AddressSanitizer)"
   - The debugger will launch with ASan enabled

## VS Code Tasks

The following tasks are available in the VS Code task runner (`Ctrl+Shift+P` → "Tasks: Run Task"):

- `configure-msan-debug`: Configure CMake for MemorySanitizer
- `build-msan-debug`: Build with MemorySanitizer
- `configure-asan-debug`: Configure CMake for AddressSanitizer  
- `build-asan-debug`: Build with AddressSanitizer
- `configure-debug`: Configure regular debug build
- `build-debug`: Build regular debug version

## Debug Configurations

Three debug configurations are available:

1. **Debug aiSocksExample (MemorySanitizer)**: Runs with MSan runtime options
2. **Debug aiSocksExample (AddressSanitizer)**: Runs with ASan runtime options
3. **Debug aiSocksExample (Debug)**: Regular debug build

## MemorySanitizer Notes

⚠️ **Important**: MemorySanitizer requires that **all** code, including system libraries, be built with MSan instrumentation. This means:

- Use a MSan-instrumented C++ standard library
- Use MSan-instrumented system libraries
- Avoid linking against non-instrumented libraries

On macOS, you may need to use a custom LLVM toolchain with MSan support:

```bash
# Example using Homebrew LLVM with MSan
export CC=/usr/local/opt/llvm/bin/clang
export CXX=/usr/local/opt/llvm/bin/clang++
```

## Runtime Options

### MemorySanitizer (MSAN_OPTIONS)
- `halt_on_error=1`: Stop on first error
- `report_umrs=1`: Report uninitialized memory reads
- `print_stats=1`: Print statistics at exit

### AddressSanitizer (ASAN_OPTIONS)
- `halt_on_error=1`: Stop on first error
- `detect_stack_use_after_return=1`: Detect stack use after return
- `strict_string_checks=1`: Strict string operations
- `detect_leaks=1`: Enable leak detection

## Troubleshooting

### MSan: "undefined symbol" errors
This usually means you're linking against non-instrumented libraries. You need to either:
1. Build those libraries with MSan, or
2. Use a MSan-instrumented runtime environment

### ASan: No output
Check that your code actually has memory issues. ASan only reports on actual errors.

### Build failures
Ensure you're using Clang/LLVM as the compiler, as sanitizers are GCC/Clang-specific.

## Performance Impact

- **MSan**: 2-3x slowdown, significant memory overhead
- **ASan**: 1.5-2x slowdown, moderate memory overhead

Use these tools primarily for debugging, not production performance testing.
