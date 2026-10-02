. "$PSScriptRoot\common.ps1"
& $PythonExe "$PSScriptRoot\run.py"
if ($LASTEXITCODE -ne 0) {
    Write-Host 'See the specific error above. Logs: runtime\broker.log and runtime\dashboard.log' -ForegroundColor Yellow
    exit $LASTEXITCODE
}
