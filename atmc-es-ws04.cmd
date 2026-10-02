@echo off
cd /d "%~dp0"
set "ATMC_PORT=COM11"
set /p "ATMC_PORT=Nhap cong COM cua ATMC/ESP32 [COM11]: "
if not defined ATMC_PORT set "ATMC_PORT=COM11"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows\atmc_es_ws04_flash.ps1" -Port "%ATMC_PORT%"
pause

