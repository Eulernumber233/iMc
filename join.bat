@echo off
:: Join (客户端) — 默认连接 127.0.0.1:60011
cd /d "%~dp0"
start "iMc Join" x64\Debug\iMc.exe --join 127.0.0.1 60011
