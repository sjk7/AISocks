# AISocks Windows Tests - Wine Testing Guide

## Files Prepared for Wine Testing

### 📁 **Directory Contents:**
- **30 Windows executables** (.exe files)
- **Static library** (lib/libaiSocksLib.a)
- **Test scripts** (.bat files)
- **All files are statically linked** - no DLL dependencies

### 🚀 **How to Test in WineSkin Winery:**

1. **Copy to Wine Bottle:**
   - Open WineSkin Winery
   - Select your Windows bottle
   - Navigate to the bottle's `drive_c` folder
   - Copy the entire `windows-test` folder there

2. **Run Tests:**
   
   **Quick Test (5 tests):**
   ```
   run_tests.bat
   ```
   
   **Full Test Suite (20 tests):**
   ```
   run_all_tests.bat
   ```
   
   **Examples Only:**
   ```
   run_examples.bat
   ```

### 🧪 **Expected Results:**

**Fast Tests Should Complete Quickly:**
- `test_server_base_minimal.exe` - ~0.03s
- `test_server_base_echo_simple.exe` - ~0.14s  
- `test_server_base_no_timeout.exe` - ~0.03s

**All Tests Should Pass:**
- 20 tests total
- All should show "PASS" status
- No error messages or crashes

### 📊 **Performance Expectations:**
- **Total test time**: ~2-5 seconds in Wine
- **Memory usage**: ~10-30MB per executable
- **No network dependencies** (all use localhost)

### 🔧 **Troubleshooting:**

**If tests hang:**
- Check Wine network configuration
- Ensure localhost resolves correctly
- Try running individual executables

**If tests fail:**
- Check Wine Windows version compatibility
- Verify all files copied correctly
- Run with `wineconsole` for error output

### 🎯 **Key Tests to Monitor:**

1. **test_server_base_echo_simple.exe** - Our fixed echo server
2. **test_error_messages.exe** - DNS error message fixes  
3. **test_server_base_minimal.exe** - Fastest test (0.03s)
4. **http_server.exe** - HTTP server functionality

### 📈 **Success Criteria:**
- ✅ All 20 tests pass
- ✅ No crashes or hangs
- ✅ Performance similar to native Windows
- ✅ Echo server works correctly
- ✅ DNS error messages display properly
