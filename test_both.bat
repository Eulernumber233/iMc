@echo off
:: 双开：Host=左上 + Join=右上
:: 位置按 1920x1080 屏幕计算：窗口 1200x900
cd /d "%~dp0"
echo Starting Host (top-left)...
start "iMc Host" x64\Debug\iMc.exe --host 60011 --winpos 0 100
timeout /t 2 /nobreak >nul
echo Starting Join (top-right)...
start "iMc Join" x64\Debug\iMc.exe --join 127.0.0.1 60011 --winpos 1260 100
echo Both launched.
