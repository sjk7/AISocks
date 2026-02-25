@echo off
echo === Running AISocks Windows Examples ===
echo.

echo [1/9] Main example...
aiSocksExample.exe
echo.

echo [2/9] Non-blocking example...
aiSocksNonBlocking.exe
echo.

echo [3/9] Blocking state test...
aiSocksTestBlocking.exe
echo.

echo [4/9] IPv6 test...
aiSocksTestIPv6.exe
echo.

echo [5/9] IP utilities test...
aiSocksTestIPUtils.exe
echo.

echo [6/9] Move semantics test...
aiSocksTestMove.exe
echo.

echo [7/9] Peer logger example...
aiSocksPeerLogger.exe
echo.

echo [8/9] Google client example...
aiSocksGoogleClient.exe
echo.

echo [9/9] HTTP server (run for 3 seconds)...
start /B http_server.exe
timeout /t 3 /nobreak >nul
taskkill /F /IM http_server.exe >nul 2>&1
echo HTTP server stopped
echo.

echo === All Examples Completed ===
pause
