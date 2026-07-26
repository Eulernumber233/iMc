@echo off
:: Host (房主) — 窗口定位到左上角 (Release)
cd /d "%~dp0"
start "iMc Host" x64\Release\iMc.exe --host 60011 --winpos 0 0