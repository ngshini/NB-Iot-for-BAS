@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File windows\serial.ps1
if errorlevel 1 pause
