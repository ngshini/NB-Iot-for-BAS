. "$PSScriptRoot\common.ps1"
$config = Get-Content -LiteralPath "$ProjectRoot\config.local.json" -Raw | ConvertFrom-Json
Start-Process "http://127.0.0.1:$($config.http_port)/serial"
