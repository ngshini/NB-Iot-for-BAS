# Compile, flash and monitor the ATMC/ESP32 + TF03 Wi-Fi MQTT firmware.
# Examples:
#   .\windows\atmc_tf03_flash.ps1 -CompileOnly
#   .\windows\atmc_tf03_flash.ps1 -Port COM16
param(
    [string]$Port = 'COM16',
    [int]$UploadBaud = 115200,
    [switch]$MonitorOnly,
    [switch]$CompileOnly,
    [int]$Seconds = 0
)
$ErrorActionPreference = 'Stop'
$runner = Join-Path $PSScriptRoot 'bas_flash.ps1'
& $runner -Sketch 'tf03_wifi_mqtts' -Port $Port -UploadBaud $UploadBaud `
    -MonitorOnly:$MonitorOnly -CompileOnly:$CompileOnly -Seconds $Seconds
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
