param([int]$Port = 8092)
$ErrorActionPreference = 'Stop'
$site = Join-Path $PSScriptRoot 'es_ws04_monitor'
$index = Join-Path $site 'index.html'
if (-not (Test-Path -LiteralPath $index)) { throw "Missing $index" }

$busy = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if ($busy) { throw "Port $Port is already in use. Stop that server or choose another port." }

$url = "http://127.0.0.1:$Port/"
$listener = [Net.HttpListener]::new()
$listener.Prefixes.Add($url)
$listener.Start()

$browser = @(
  "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
  "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
  "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe",
  "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
  "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($browser) { Start-Process -FilePath $browser -ArgumentList $url }
else { Start-Process $url }

Write-Host "ES-WS-04 Monitor: $url  (Ctrl+C to stop the local web server)"
try {
  while ($listener.IsListening) {
    $context = $listener.GetContext()
    if ($context.Request.Url.AbsolutePath -ne '/') {
      $context.Response.StatusCode = 404
      $context.Response.Close()
      continue
    }
    $bytes = [IO.File]::ReadAllBytes($index)
    $context.Response.ContentType = 'text/html; charset=utf-8'
    $context.Response.ContentLength64 = $bytes.Length
    $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
    $context.Response.Close()
  }
} finally {
  $listener.Stop()
  $listener.Close()
}

