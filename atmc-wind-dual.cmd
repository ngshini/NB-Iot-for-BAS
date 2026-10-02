@echo off
cd /d "%~dp0"
set "ATMC_PORT="
set /p "ATMC_PORT=Nhap cong COM cua ATMC (vi du COM11): "
if not defined ATMC_PORT exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows\bas_flash.ps1" -Sketch wind_dual_rs485 -Port "%ATMC_PORT%"
pause
