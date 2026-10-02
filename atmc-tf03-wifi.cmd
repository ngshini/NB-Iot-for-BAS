@echo off
cd /d "%~dp0"
set "ATMC_PORT="
set /p "ATMC_PORT=Nhap cong COM cua ESP32 tren bo ATMC (vi du COM16): "
if not defined ATMC_PORT exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows\atmc_tf03_flash.ps1" -Port "%ATMC_PORT%"
pause
