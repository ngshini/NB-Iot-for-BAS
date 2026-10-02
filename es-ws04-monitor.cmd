@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows\es_ws04_monitor.ps1"
if errorlevel 1 pause

