@echo off
:: 三人联机测试：1 房主 + 2 客户端
cd /d "%~dp0"
echo ========================================
echo Starting Host...
echo ========================================
start "iMc Host" x64\Debug\iMc.exe --host 60011
timeout /t 3 /nobreak >nul

echo ========================================
echo Starting Client 1...
echo ========================================
start "iMc Client1" x64\Debug\iMc.exe --join 127.0.0.1 60011
timeout /t 2 /nobreak >nul

echo ========================================
echo Starting Client 2...
echo ========================================
start "iMc Client2" x64\Debug\iMc.exe --join 127.0.0.1 60011

echo ========================================
echo All 3 processes launched.
echo ========================================
