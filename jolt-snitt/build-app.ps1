# Windows counterpart of ./build-app. --signable is macOS-only: it needs Chez's
# headers and serves code signing, while Windows binaries are unsigned.
param([ValidateSet('Dev', 'Release', 'All')][string]$Mode = 'All', [string]$QtBin = $env:SNITT_QT_BIN)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build-native.ps1') -QtBin $QtBin
. (Join-Path $PSScriptRoot 'windows-env.ps1')
Push-Location $PSScriptRoot
try {
    if ($Mode -ne 'Release') {
        & $jolt build -m snitt.app --dev -o ../build/jolt-snitt/snitt-jolt-dev
        if ($LASTEXITCODE -ne 0) { throw 'Jolt development build failed.' }
    }
    if ($Mode -ne 'Dev') {
        & $jolt build -m snitt.app --opt --closed-world -o ../build/jolt-snitt/snitt-jolt
        if ($LASTEXITCODE -ne 0) { throw 'Jolt release build failed.' }
    }
} finally { Pop-Location }
