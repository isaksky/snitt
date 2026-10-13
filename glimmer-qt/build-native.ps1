[CmdletBinding(PositionalBinding = $false)]
param([string]$QtBin = $env:SNITT_QT_BIN,
      [Parameter(ValueFromRemainingArguments = $true)][string[]]$QmakeArgs = @())
$ErrorActionPreference = 'Stop'
if (!$QtBin) {
    $scoopQt = Join-Path $env:USERPROFILE 'scoop\apps\msys2\current\ucrt64\bin'
    if (Test-Path "$scoopQt\qmake6.exe") { $QtBin = $scoopQt }
    else { $QtBin = Split-Path (Get-Command qmake6.exe -ErrorAction Stop).Source -Parent }
}
$qmake = (Resolve-Path (Join-Path $QtBin 'qmake6.exe')).Path
if ((& $qmake -query QT_VERSION) -notmatch '^6\.') { throw 'Qt 6 is required.' }
$env:PATH = "$QtBin;$env:PATH"
$build = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force $build | Out-Null
Push-Location $build
try {
    # Build into build\ itself so deps.edn finds build\glimmer_qt.dll.
    $arguments = @('CONFIG+=release', 'CONFIG-=debug_and_release') + $QmakeArgs
    # qmake does not invalidate objects when only SDK paths or defines change.
    # MSYS2's generated clean target needs rm, so remove what it would.
    $config = @($qmake) + $arguments
    if ((Test-Path Makefile) -and (!(Test-Path build-config) -or
        ((Get-Content build-config) -join "`n") -ne ($config -join "`n"))) {
        Remove-Item *.o, *.moc, moc_*, qrc_* -ErrorAction SilentlyContinue
    }
    & $qmake (Join-Path $PSScriptRoot 'native\bridge.pro') @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Glimmer qmake failed.' }
    & mingw32-make "-j$env:NUMBER_OF_PROCESSORS"
    if ($LASTEXITCODE -ne 0) { throw 'Glimmer native build failed.' }
    $config | Set-Content build-config
    $qmake | Set-Content qmake-path
} finally { Pop-Location }
