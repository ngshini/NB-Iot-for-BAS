@echo off
cd /d "%~dp0"
if not exist config.local.json (
  powershell -NoProfile -ExecutionPolicy Bypass -File windows\setup.ps1
  if errorlevel 1 goto failure
)
powershell -NoProfile -ExecutionPolicy Bypass -File windows\start.ps1
if errorlevel 1 goto failure
exit /b 0
:failure
pause
exit /b 1
