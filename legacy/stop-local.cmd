@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File windows\stop.ps1
pause
