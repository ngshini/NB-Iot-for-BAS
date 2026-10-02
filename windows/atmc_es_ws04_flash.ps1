# Compile, flash and monitor the ES-WS-04 RS485 firmware on ATMC/ESP32.
param(
    [string]$Port = 'COM11',
    [int]$UploadBaud = 115200,
    [switch]$MonitorOnly,
    [switch]$CompileOnly,
    [int]$Seconds = 0
)
$ErrorActionPreference = 'Stop'
$runner = Join-Path $PSScriptRoot 'bas_flash.ps1'
& $runner -Sketch 'es_ws_04_rs485' -Port $Port -UploadBaud $UploadBaud `
    -MonitorOnly:$MonitorOnly -CompileOnly:$CompileOnly -Seconds $Seconds
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

