@echo off
echo === Running ALL AISocks Windows Tests ===
echo.

REM Run each test and report results
set PASSED=0
set FAILED=0

call :run_test test_server_base_minimal.exe
call :run_test test_server_base_no_timeout.exe
call :run_test test_server_base_echo_simple.exe
call :run_test test_server_base_simple.exe
call :run_test test_server_base.exe
call :run_test test_error_messages.exe
call :run_test test_socket_basics.exe
call :run_test test_ip_utils.exe
call :run_test test_blocking.exe
call :run_test test_move_semantics.exe
call :run_test test_loopback_tcp.exe
call :run_test test_error_handling.exe
call :run_test test_construction.exe
call :run_test test_new_features.exe
call :run_test test_poller.exe
call :run_test test_tcp_socket.exe
call :run_test test_simple_client.exe
call :run_test test_socket_factory.exe
call :run_test test_http_request.exe
call :run_test test_url_codec.exe

echo.
echo === Test Summary ===
echo Passed: %PASSED%
echo Failed: %FAILED%
echo Total: 20
if %FAILED% EQU 0 (
    echo ALL TESTS PASSED!
) else (
    echo %FAILED% tests failed
)
pause
goto :eof

:run_test
echo Running %~1...
%~1 >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo   PASS
    set /a PASSED+=1
) else (
    echo   FAIL (exit code %ERRORLEVEL%)
    set /a FAILED+=1
)
goto :eof
