$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$PythonExe = Join-Path $ProjectRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $PythonExe)) {
    $PythonExe = Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'
}
if (-not (Test-Path -LiteralPath $PythonExe)) {
    $foundPython = Get-Command python.exe -ErrorAction SilentlyContinue
    if (-not $foundPython) { throw 'Install Python 3.10+ and add python.exe to PATH.' }
    $PythonExe = $foundPython.Source
}
