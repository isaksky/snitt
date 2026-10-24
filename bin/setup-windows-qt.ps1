param([Parameter(Mandatory = $true)][string]$MsysRoot)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path $PSScriptRoot -Parent
$pins = Get-Content "$root/scripts/windows-qt-packages.json" -Raw | ConvertFrom-Json
$downloads = Join-Path $root 'build/tools/windows-qt-packages'
New-Item -ItemType Directory -Force $downloads | Out-Null
$packages = @()
foreach ($pin in $pins) {
    $path = Join-Path $downloads $pin.file
    $url = "https://repo.msys2.org/mingw/ucrt64/$($pin.file)"
    if (!(Test-Path -LiteralPath $path) -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256) {
        Invoke-WebRequest -Uri $url -OutFile $path
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256) {
        throw "Qt package checksum mismatch: $($pin.file)"
    }
    Invoke-WebRequest -Uri "$url.sig" -OutFile "$path.sig"
    $packages += $path
}
# Install all Qt modules together, retaining pacman's dependency/signature checks.
# The rolling repository supplies the remaining SDK dependencies; the build and
# packaging gates still reject an incompatible toolchain or FFmpeg ABI.
& "$MsysRoot/usr/bin/pacman.exe" -U --noconfirm @packages
if ($LASTEXITCODE -ne 0) { throw 'Could not install the pinned Qt packages.' }
