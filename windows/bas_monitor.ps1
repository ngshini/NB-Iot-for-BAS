# Serve windows\bas_monitor on http://127.0.0.1:<Port> and open it in Chrome/Edge.
# Web Serial needs a secure origin (127.0.0.1) and a Chromium browser. Ctrl+C stops the server.
param([int]$Port = 8090)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$site = Join-Path $PSScriptRoot 'bas_monitor'
$url = "http://127.0.0.1:$Port/"

$python = (Get-Command py.exe -ErrorAction SilentlyContinue).Source
$pyArgs = @('-3')
if (-not $python) { $python = (Get-Command python.exe -ErrorAction SilentlyContinue).Source; $pyArgs = @() }
if (-not $python) { throw 'Install Python 3 (py.exe or python.exe on PATH).' }

$busy = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if ($busy) {
    $busy | Select-Object -ExpandProperty OwningProcess -Unique | ForEach-Object {
        Stop-Process -Id $_ -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 500
}

$browser = @(
    "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
    "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

$bridge = Join-Path $PSScriptRoot 'bas_monitor_server.py'
$ca = Join-Path $root 'firmware\bas_mqtts\mosquitto.org.pem'
$server = Start-Process -FilePath $python -NoNewWindow -PassThru -ArgumentList (
    $pyArgs + @("`"$bridge`"", '--port', "$Port", '--site', "`"$site`"", '--cafile', "`"$ca`""))
Start-Sleep -Seconds 1
if ($server.HasExited) { throw 'The local web server did not start.' }
if ($browser) { Start-Process -FilePath $browser -ArgumentList $url } else { Start-Process $url }
Write-Host "BAS Serial Monitor: $url  (Ctrl+C to stop)"
try { Wait-Process -Id $server.Id } finally { if (-not $server.HasExited) { Stop-Process -Id $server.Id } }
