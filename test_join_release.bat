@echo off
:: Join (客户端) — 窗口定位到右上角 (Release)
cd /d "%~dp0"
start "iMc Join" x64\Release\iMc.exe --join 127.0.0.1 60011 --winpos 960 0