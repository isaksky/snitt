# Windows counterpart of ./run: .\jolt-snitt\run.ps1 [image-path | --show | --background | --quit]
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build-native.ps1')
. (Join-Path $PSScriptRoot 'windows-env.ps1')
Push-Location $PSScriptRoot
try {
    & $jolt -M:run @args
    exit $LASTEXITCODE
} finally { Pop-Location }
