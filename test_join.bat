@echo off
:: Join (客户端) — 窗口定位到右上角
cd /d "%~dp0"
start "iMc Join" x64\Debug\iMc.exe --join 127.0.0.1 60011 --winpos 960 0
