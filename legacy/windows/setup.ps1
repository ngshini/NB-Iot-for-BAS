param([string]$PublicHost, [int]$PublicPort, [string]$Apn)
. "$PSScriptRoot\common.ps1"
$paramsList = @("$PSScriptRoot\configure.py")
if ($PSBoundParameters.ContainsKey('PublicHost')) { $paramsList += @('--host', $PublicHost) }
if ($PSBoundParameters.ContainsKey('PublicPort')) { $paramsList += @('--port', "$PublicPort") }
if ($PSBoundParameters.ContainsKey('Apn')) { $paramsList += @('--apn', $Apn) }
& $PythonExe @paramsList
if ($LASTEXITCODE -ne 0) { throw 'Project configuration failed.' }
