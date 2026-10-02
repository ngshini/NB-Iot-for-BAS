. "$PSScriptRoot\common.ps1"
$config = Get-Content -LiteralPath "$ProjectRoot\config.local.json" -Raw | ConvertFrom-Json
$ngrok = Get-Command ngrok.exe -ErrorAction SilentlyContinue
if (-not $ngrok) { throw 'Install ngrok from https://ngrok.com/downloads/windows and configure your own authtoken first.' }
Write-Host 'Copy the public TCP hostname and port to setup.ps1 -PublicHost ... -PublicPort ...'
& $ngrok.Source tcp "127.0.0.1:$($config.mqtt_port)"
