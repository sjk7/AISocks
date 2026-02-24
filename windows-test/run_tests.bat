@echo off
echo === Running AISocks Windows Tests ===
echo.

echo [1/5] Running main example...
aiSocksExample.exe
if %ERRORLEVEL% NEQ 0 echo Example failed!
echo.

echo [2/5] Running HTTP server example (5 seconds)...
start /B http_server.exe
timeout /t 5 /nobreak >nul
taskkill /F /IM http_server.exe >nul 2>&1
echo HTTP server test completed
echo.

echo [3/5] Running fast tests...
test_server_base_minimal.exe
echo.

echo [4/5] Running echo server test...
test_server_base_echo_simple.exe
echo.

echo [5/5] Running error messages test...
test_error_messages.exe
echo.

echo === All Windows Tests Completed ===
pause
