[CmdletBinding(PositionalBinding = $false)]
param([string]$QtBin = $env:SNITT_QT_BIN,
      [Parameter(ValueFromRemainingArguments = $true)][string[]]$QmakeArgs = @())
$ErrorActionPreference = 'Stop'
$glimmer = Join-Path $PSScriptRoot '..\glimmer-qt'
& (Join-Path $glimmer 'build-native.ps1') -QtBin $QtBin @QmakeArgs
$qmake = (Get-Content (Join-Path $glimmer 'build\qmake-path')).Trim()
$env:PATH = "$(Split-Path $qmake -Parent);$env:PATH"
$build = Join-Path $PSScriptRoot '..\build\jolt-snitt'
New-Item -ItemType Directory -Force $build | Out-Null
Push-Location $build
try {
    # MSYS2's generated clean target needs rm, so remove what it would.
    if ((Test-Path Makefile) -and (Test-Path qmake-path) -and (Get-Content qmake-path).Trim() -ne $qmake) {
        Remove-Item *.o, *.moc, moc_*, qrc_* -ErrorAction SilentlyContinue
    }
    & $qmake (Join-Path $PSScriptRoot 'native\snitt_jolt.pro') 'CONFIG+=release' 'CONFIG-=debug_and_release'
    if ($LASTEXITCODE -ne 0) { throw 'Snitt qmake failed.' }
    & mingw32-make "-j$env:NUMBER_OF_PROCESSORS"
    if ($LASTEXITCODE -ne 0) { throw 'Snitt native build failed.' }
    $qmake | Set-Content qmake-path
} finally { Pop-Location }
