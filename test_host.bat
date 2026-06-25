@echo off
:: Host (房主) — 窗口定位到左上角
cd /d "%~dp0"
start "iMc Host" x64\Debug\iMc.exe --host 60011 --winpos 0 0
