@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File windows\bas_monitor.ps1
if errorlevel 1 pause
