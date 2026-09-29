[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string] $Version,
    [string] $ReleaseDirectory = 'build\release',
    [string] $ManifestPath = 'bucket\snitt.json',
    [string] $ScoopCommand
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne 'Win32NT') { throw 'Run the Scoop integration test on Windows.' }
if (!$ScoopCommand) { $ScoopCommand = (Get-Command scoop -ErrorAction Stop).Source }
$scoopRoot = if ($env:SCOOP) { $env:SCOOP } else { Join-Path $env:USERPROFILE 'scoop' }
$appRoot = Join-Path $scoopRoot 'apps\snitt'
if (Test-Path -LiteralPath $appRoot) { throw "A Snitt Scoop installation already exists at $appRoot. This test will not replace it." }
$releaseRoot = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$release = $manifest.architecture.'64bit'
$archiveName = "snitt_${Version}_windows_amd64.zip"
$archivePath = Join-Path $releaseRoot $archiveName
if ($manifest.version -ne $Version -or !$release.url.EndsWith("/v$Version/$archiveName")) {
    throw 'The Scoop manifest and archive version do not match.'
}
if ($release.hash -ne (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()) {
    throw 'The Scoop manifest SHA-256 does not match the package.'
}
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$standalone = Get-ItemProperty -LiteralPath $runKey -Name 'snitt' -ErrorAction SilentlyContinue
$standaloneValue = if ($standalone) { $standalone.snitt } else { $null }
if ($standaloneValue -eq ('"' + (Join-Path $env:LOCALAPPDATA 'Programs\snitt\snitt.exe') + '" --background')) {
    throw 'Remove the standalone Snitt installation before running the Scoop integration test.'
}

function Invoke-Scoop {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]] $Arguments)
    $scoopExitCode = & {
        param($Command, $CommandArguments)
        Set-StrictMode -Off
        & $Command @CommandArguments | Out-Host
        $LASTEXITCODE
    } $ScoopCommand $Arguments
    if ($scoopExitCode -ne 0) { throw "scoop $($Arguments -join ' ') failed ($scoopExitCode)" }
}

function Assert-Installation {
    $installed = Join-Path $appRoot 'current'
    foreach ($relative in 'snitt.exe', 'Qt6Core.dll', 'plugins\platforms\qwindows.dll', 'qt.conf', 'Scoop.ps1', 'VERSION',
        'THIRD-PARTY-NOTICES.txt', 'licenses\qt6-base\LGPL-3.0-only.txt', 'licenses\qt6-declarative\LGPL-3.0-only.txt') {
        if (!(Test-Path -LiteralPath (Join-Path $installed $relative) -PathType Leaf)) { throw "Missing package file: $relative" }
    }
    if ((Get-Content -LiteralPath (Join-Path $installed 'VERSION') -Raw).Trim() -ne $Version) { throw 'Installed VERSION is incorrect.' }
    $startup = (Get-ItemProperty -LiteralPath $runKey -Name 'snitt-Scoop').'snitt-Scoop'
    if ($startup -ne ('"' + (Join-Path $installed 'snitt.exe') + '" --background')) { throw "Incorrect login command: $startup" }
    $shim = Join-Path $scoopRoot 'shims\snitt.exe'
    if (!(Test-Path -LiteralPath $shim)) { throw 'Scoop did not create the snitt command shim.' }
    $sessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        $resident = @(Get-Process snitt -ErrorAction SilentlyContinue | Where-Object {
            $_.SessionId -eq $sessionId -and $_.Path -and $_.Path.StartsWith($appRoot + '\', [StringComparison]::OrdinalIgnoreCase)
        })
        if ($resident.Count) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($resident.Count -ne 1) { throw "Expected one Scoop background process; found $($resident.Count)." }
    Start-Sleep -Milliseconds 500
    if ($resident[0].HasExited) { throw 'The packaged background process exited during startup.' }
    $ffmpegOutput = & (Join-Path $scoopRoot 'shims\ffmpeg.exe') -version
    $ffmpegExitCode = $LASTEXITCODE
    $ffmpegOutput | Select-Object -First 1
    if ($ffmpegExitCode -ne 0) { throw "The ffmpeg Scoop dependency cannot run ($ffmpegExitCode)." }
}

# Prime Scoop's normal download cache with the local release asset. Scoop still
# verifies the committed hash, so this test works before publishing the release.
$cachePath = & {
    # Scoop supports Windows PowerShell's default permissive mode; do not make
    # its optional configuration properties inherit this script's strictness.
    Set-StrictMode -Off
    . (Join-Path $scoopRoot 'apps\scoop\current\lib\core.ps1')
    cache_path 'snitt' $Version $release.url
}
New-Item -ItemType Directory -Force (Split-Path $cachePath -Parent) | Out-Null
Copy-Item -LiteralPath $archivePath -Destination $cachePath -Force
$testBucketName = 'snitt-test-' + [Guid]::NewGuid().ToString('N')
$testBucketRoot = Join-Path $scoopRoot ('buckets\' + $testBucketName)
New-Item -ItemType Directory -Path (Join-Path $testBucketRoot 'bucket') | Out-Null
Copy-Item -LiteralPath $ManifestPath -Destination (Join-Path $testBucketRoot 'bucket\snitt.json')
try {
    Invoke-Scoop install "$testBucketName/snitt"
    Assert-Installation
    # Scoop checks running processes before update hooks, so quit first using
    # the same scoped helper supplied by the release package.
    & (Join-Path $appRoot 'current\Scoop.ps1') -Action Stop
    Invoke-Scoop update snitt --force
    Assert-Installation
    Invoke-Scoop uninstall snitt
    if (Test-Path -LiteralPath $appRoot) { throw 'Scoop uninstall left the application directory.' }
    if (Get-ItemProperty -LiteralPath $runKey -Name 'snitt-Scoop' -ErrorAction SilentlyContinue) { throw 'Scoop uninstall left the login entry.' }
    $after = Get-ItemProperty -LiteralPath $runKey -Name 'snitt' -ErrorAction SilentlyContinue
    $afterValue = if ($after) { $after.snitt } else { $null }
    if ($afterValue -ne $standaloneValue) { throw 'Scoop changed the standalone startup value.' }
    Write-Output "Verified Scoop install, forced update, resident startup, Qt and ffmpeg deployment, and uninstall for Snitt $Version."
} finally {
    # Retain a failed install and its bucket for diagnosis. Remove only the
    # temporary bucket when the app was completely uninstalled.
    if (!(Test-Path -LiteralPath $appRoot)) { Remove-Item -LiteralPath $testBucketRoot -Recurse -Force }
}
