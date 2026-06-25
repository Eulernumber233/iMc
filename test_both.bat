@echo off
:: 双开：先起 Host，等 2 秒再起 Join
cd /d "%~dp0"
echo Starting Host...
start "iMc Host" x64\Debug\iMc.exe --host 60011
timeout /t 2 /nobreak >nul
echo Starting Join...
start "iMc Join" x64\Debug\iMc.exe --join 127.0.0.1 60011
echo Both launched.
