param([int]$Count = 10, [double]$Interval = 2)
. "$PSScriptRoot\common.ps1"
& $PythonExe "$PSScriptRoot\simulate.py" --count $Count --interval $Interval
if ($LASTEXITCODE -ne 0) { throw 'MQTT simulation failed.' }
