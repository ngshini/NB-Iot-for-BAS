# Build, flash and monitor firmware/bas_mqtts (NB-IoT + Wi-Fi) on an ESP32 DevKit.
#   .\windows\bas_flash.ps1                 # compile + upload + log Serial
#   .\windows\bas_flash.ps1 -MonitorOnly    # only log Serial
#   .\windows\bas_flash.ps1 -CompileOnly    # verify firmware without flashing
#   .\windows\bas_flash.ps1 -Restore backups\<file>.bin   # write a full 4 MB backup back
# Upload uses 115200 baud: faster rates corrupted long transfers on this CP210x link.
# esptool verifies each written region (MD5), so a corrupt upload fails instead of booting.
#   .\windows\bas_flash.ps1 -Sketch <folder>   # another sketch under firmware\
param(
    [string]$Sketch = 'bas_mqtts',
    [string]$Port = 'COM16',
    [int]$UploadBaud = 115200,
    [switch]$MonitorOnly,
    [switch]$CompileOnly,
    [switch]$ResetBoard,
    [string]$Restore,
    [int]$Seconds = 0
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cli = 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
$sketchDir = Join-Path (Join-Path $root 'firmware') $Sketch
if (-not (Test-Path -LiteralPath $sketchDir)) { throw "Sketch not found: $sketchDir" }
$fqbn = "esp32:esp32:esp32:UploadSpeed=$UploadBaud"

if ($Restore) {
    $esptool = Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\*\esptool.exe" |
        Select-Object -Last 1
    & $esptool.FullName --port $Port --baud $UploadBaud write-flash 0 $Restore
    if ($LASTEXITCODE -ne 0) { throw 'Restore failed.' }
    return
}

if (-not $MonitorOnly) {
    if (-not (Test-Path -LiteralPath $cli)) { throw "Arduino CLI not found: $cli. Install Arduino IDE 2.x first." }
    & $cli compile --fqbn $fqbn $sketchDir
    if ($LASTEXITCODE -ne 0) { throw "Compile failed (exit code $LASTEXITCODE). See Arduino's error above. If esp32:esp32 is missing, install 'esp32 by Espressif Systems' 3.3.12 in Boards Manager. The Wi-Fi firmware also requires PubSubClient in Library Manager." }
    if ($CompileOnly) { return }
    & $cli upload --fqbn $fqbn -p $Port $sketchDir
    if ($LASTEXITCODE -ne 0) { throw "Upload failed. Close any Serial Monitor using $Port and retry." }
}

# Log Serial to the console and runtime\bas-serial-<time>.log. Ctrl+C stops.
$logDir = Join-Path $root 'runtime'
New-Item -ItemType Directory -Force $logDir | Out-Null
$log = Join-Path $logDir ("bas-serial-{0:yyyyMMdd-HHmmss}.log" -f (Get-Date))
$serial = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$serial.ReadTimeout = 500
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.Open()
# Avoid an intentional reset on a normal monitor connection. Some ESP32 USB
# bridges can still reset when Windows opens the port; do not leave COM open
# during the final NB-IoT test. Use -ResetBoard only when a boot log is needed.
if ($ResetBoard) {
    $serial.RtsEnable = $true
    Start-Sleep -Milliseconds 120
    $serial.RtsEnable = $false
}
Write-Host "Logging $Port to $log (Ctrl+C to stop)"
$end = if ($Seconds -gt 0) { (Get-Date).AddSeconds($Seconds) } else { [datetime]::MaxValue }
try {
    while ((Get-Date) -lt $end) {
        $text = $serial.ReadExisting()
        if ($text) {
            Write-Host -NoNewline $text
            [System.IO.File]::AppendAllText($log, $text)
        }
        Start-Sleep -Milliseconds 50
    }
} finally {
    $serial.Close()
}
