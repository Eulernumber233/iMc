@echo off
:: Host (房主) — 默认端口 60011，自动创建新世界
cd /d "%~dp0"
start "iMc Host" x64\Debug\iMc.exe --host 60011
