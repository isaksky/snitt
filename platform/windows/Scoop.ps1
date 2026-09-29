[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Check', 'Install', 'Stop', 'Uninstall')]
    [string] $Action,
    [string] $InstallDirectory = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runName = 'snitt-Scoop'
$installRoot = [IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
$appRoot = Split-Path $installRoot -Parent
$exe = Join-Path $installRoot 'snitt.exe'
$command = '"' + $exe + '" --background'
$sessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId

function Get-StartupCommand([string] $Name) {
    $item = Get-ItemProperty -LiteralPath $runKey -Name $Name -ErrorAction SilentlyContinue
    if ($item) { return $item.$Name }
    return $null
}

function Get-OwnedProcess {
    # Scoop can pass a version directory or its current junction. Restrict stop
    # requests to this app's Scoop directory and this interactive session.
    @(Get-Process snitt -ErrorAction SilentlyContinue | Where-Object {
        $_.SessionId -eq $sessionId -and $_.Path -and
        $_.Path.StartsWith($appRoot + '\', [StringComparison]::OrdinalIgnoreCase)
    })
}

function Stop-OwnedProcess {
    $running = @(Get-OwnedProcess)
    if (!$running.Count) { return }
    $request = Start-Process $exe -ArgumentList '--quit' -PassThru
    if (!$request.WaitForExit(5000)) { throw 'Snitt did not accept the quit request. Quit it from its tray menu and retry.' }
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        if (!@(Get-OwnedProcess).Count) { return }
        Start-Sleep -Milliseconds 100
    }
    throw 'Snitt is still running. Finish recording or editing, then quit it from its tray menu and retry.'
}

if ($Action -eq 'Check' -or $Action -eq 'Install') {
    $standaloneExe = Join-Path $env:LOCALAPPDATA 'Programs\snitt\snitt.exe'
    $standaloneCommand = '"' + $standaloneExe + '" --background'
    if ((Get-StartupCommand 'snitt') -eq $standaloneCommand) {
        throw 'The standalone Snitt installer is active. Run %LOCALAPPDATA%\Programs\snitt\Uninstall.cmd before installing with Scoop.'
    }
    $foreign = @(Get-Process snitt -ErrorAction SilentlyContinue | Where-Object {
        $_.SessionId -eq $sessionId -and $_.Path -and
        !($_.Path.StartsWith($appRoot + '\', [StringComparison]::OrdinalIgnoreCase))
    })
    if ($foreign.Count) { throw 'Another installation of Snitt is running. Quit it before installing with Scoop.' }
    if ($Action -eq 'Check') { return }
    if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Snitt executable is missing: $exe" }
    New-Item -Path $runKey -Force | Out-Null
    Set-ItemProperty -LiteralPath $runKey -Name $runName -Value $command
    Start-Process $exe -ArgumentList '--background'
    return
}

Stop-OwnedProcess
if ($Action -eq 'Uninstall') {
    $existing = Get-StartupCommand $runName
    # During an update Scoop supplies the old version, while the Run value may
    # use current. Never remove a value owned by a different Scoop root.
    if ($existing -and $existing.StartsWith('"' + $appRoot + '\', [StringComparison]::OrdinalIgnoreCase) -and
        $existing.EndsWith('\snitt.exe" --background', [StringComparison]::OrdinalIgnoreCase)) {
        Remove-ItemProperty -LiteralPath $runKey -Name $runName
    }
}
