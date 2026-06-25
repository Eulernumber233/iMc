@echo off
:: 三人联机测试：1 房主 + 2 客户端
:: 窗口定位：Host=左上, Client1=右上, Client2=右下
:: 位置按 1920x1080 屏幕计算：窗口 1200x900
cd /d "%~dp0"
echo ========================================
echo Starting Host (top-left)...
echo ========================================
start "iMc Host" x64\Debug\iMc.exe --host 60011 --winpos 0 100
timeout /t 3 /nobreak >nul

echo ========================================
echo Starting Client 1 (top-right)...
echo ========================================
start "iMc Client1" x64\Debug\iMc.exe --join 127.0.0.1 60011 --winpos 1260 100
timeout /t 2 /nobreak >nul

echo ========================================
echo Starting Client 2 (bottom-right)...
echo ========================================
start "iMc Client2" x64\Debug\iMc.exe --join 127.0.0.1 60011 --winpos 1260 1040

echo ========================================
echo All 3 processes launched.
echo ========================================
