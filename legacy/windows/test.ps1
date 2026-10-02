. "$PSScriptRoot\common.ps1"
& $PythonExe "$PSScriptRoot\test_startup.py"
if ($LASTEXITCODE -ne 0) { throw 'Startup tests failed.' }
& $PythonExe "$PSScriptRoot\test_project.py"
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
& $PythonExe "$PSScriptRoot\test_serial.py"
if ($LASTEXITCODE -ne 0) { throw 'Serial comparison tests failed.' }
