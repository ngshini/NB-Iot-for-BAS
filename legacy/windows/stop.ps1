param([int]$KeepPid = 0, [switch]$ListOnly)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$all = @(Get-CimInstance Win32_Process -ErrorAction Stop)
# Exclude this script and its ancestors (including the new launcher).
$protected = @($PID, $KeepPid)
$ancestor = $PID
while ($ancestor -gt 0) {
    $parent = $all | Where-Object { $_.ProcessId -eq $ancestor } | Select-Object -First 1
    if (-not $parent) { break }
    $ancestor = [int]$parent.ParentProcessId
    if ($protected -contains $ancestor) { break }
    $protected += $ancestor
}
$targets = @{}
$files = @('windows\run.py', 'windows\server.py', 'windows\simulate.py')
$mqttPort = 1884
$configPath = Join-Path $root 'config.local.json'
if (Test-Path -LiteralPath $configPath) {
    $mqttPort = (Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json).mqtt_port
}
foreach ($p in $all) {
    if ($protected -contains [int]$p.ProcessId) { continue }
    $cmd = [string]$p.CommandLine
    $owned = $false
    if ($p.Name -in @('python.exe', 'pythonw.exe')) {
        foreach ($file in $files) {
            $absolute = Join-Path $root $file
            if ($cmd -match ('(?i)(?:"|\s)' + [regex]::Escape($absolute) + '(?:"|\s|$)')) { $owned = $true }
            # Relative entry points are accepted only with this project's venv executable.
            if ($cmd -match [regex]::Escape((Join-Path $root '.venv\Scripts\python.exe')) -and
                $cmd -match ('(?i)(?:"|\s)' + [regex]::Escape($file) + '(?:"|\s|$)')) { $owned = $true }
        }
    }
    if ($p.Name -eq 'mosquitto.exe' -and
        $cmd -match ('(?i)-c\s+"?' + [regex]::Escape((Join-Path $root 'runtime\mosquitto.conf')) + '(?:"|\s|$)')) { $owned = $true }
    if ($p.Name -eq 'ngrok.exe' -and $cmd -match ('(?i)\btcp\s+"?127\.0\.0\.1:' + $mqttPort + '(?:"|\s|$)')) { $owned = $true }
    if ($owned) { $targets[[int]$p.ProcessId] = $p }
}
# Capture descendants before killing parents, including venv Python and subscriber.
do {
    $changed = $false
    foreach ($p in $all) {
        $id = [int]$p.ProcessId
        if (-not $targets.ContainsKey($id) -and $targets.ContainsKey([int]$p.ParentProcessId) -and $protected -notcontains $id) {
            $targets[$id] = $p; $changed = $true
        }
    }
} while ($changed)
foreach ($id in @($targets.Keys)) {
    $original = $targets[$id]
    if ($ListOnly) { Write-Output ("Project process: {0} {1}" -f $id, $original.Name); continue }
    $current = Get-CimInstance Win32_Process -Filter "ProcessId=$id" -ErrorAction Stop
    if ($current -and $current.CreationDate -eq $original.CreationDate) {
        try { Stop-Process -Id $id -Force -ErrorAction Stop }
        catch { if (Get-Process -Id $id -ErrorAction SilentlyContinue) { throw } }
        Write-Output ("Stopped project process: {0} {1}" -f $id, $original.Name)
    }
}
if (-not $ListOnly) {
    foreach ($id in @($targets.Keys)) {
        $p = Get-Process -Id $id -ErrorAction SilentlyContinue
        if ($p) { try { $null = $p.WaitForExit(5000) } catch {} }
    }
    Write-Output 'Project stop completed. Other applications are unchanged.'
}
