[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$')]
    [string] $Repository = 'isaksky/xshot',
    [ValidatePattern('^([0-9]+\.[0-9]+\.[0-9]+)?$')]
    [string] $Version = '',
    [switch] $DownloadOnly,
    [string] $DownloadDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (!$DownloadOnly -and [Environment]::OSVersion.Platform -ne 'Win32NT') {
    throw 'Scoop installation requires Windows. Use -DownloadOnly to verify release assets on another platform.'
}
if ($DownloadOnly -and !$DownloadDirectory) { throw '-DownloadOnly requires a destination in -DownloadDirectory.' }
$ghCommand = Get-Command gh -ErrorAction Stop
$stage = Join-Path ([IO.Path]::GetTempPath()) ('xshot-scoop-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null

function Invoke-GitHub {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]] $Arguments)
    & $ghCommand @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "GitHub release download failed. Run gh auth login with an account that can access $Repository, then retry."
    }
}

try {
    $tag = if ($Version) { "v$Version" } else {
        [string](Invoke-GitHub release view --repo $Repository --json tagName --jq .tagName)
    }
    $tag = $tag.Trim()
    if ($tag -notmatch '^v([0-9]+\.[0-9]+\.[0-9]+)$') { throw "Release tag is not a stable xshot version: $tag" }
    $releaseVersion = $Matches[1]
    $archiveName = "xshot_${releaseVersion}_windows_amd64.zip"
    $checksumsName = "xshot_${releaseVersion}_checksums.txt"
    # gh owns authentication and HTTP redirects. No token is read, printed,
    # copied into a manifest, or passed to Scoop.
    Invoke-GitHub release download $tag --repo $Repository --dir $stage --pattern xshot.json --pattern $archiveName --pattern $checksumsName
    $manifestPath = Join-Path $stage 'xshot.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $release = $manifest.architecture.'64bit'
    $expectedUrl = "https://github.com/$Repository/releases/download/$tag/$archiveName"
    if ($manifest.version -ne $releaseVersion -or $manifest.homepage -ne "https://github.com/$Repository" -or $release.url -ne $expectedUrl) {
        throw 'The release manifest does not match the selected repository and version.'
    }
    if ($manifest.bin -ne 'xshot.exe' -or $manifest.depends -ne 'main/ffmpeg') {
        throw 'The release manifest does not describe the expected xshot application and FFmpeg dependency.'
    }
    if ($release.hash -notmatch '^[0-9a-f]{64}$') { throw 'The release manifest has no valid SHA-256.' }
    $archivePath = Join-Path $stage $archiveName
    $digest = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksums = @(Get-Content -LiteralPath (Join-Path $stage $checksumsName) | Where-Object {
        $_ -match ('^[0-9a-f]{64}\s+\*?' + [regex]::Escape($archiveName) + '$')
    })
    if ($digest -ne $release.hash -or $checksums.Count -ne 1 -or ($checksums[0] -split '\s+')[0] -ne $digest) {
        throw 'The downloaded ZIP does not match both the release manifest and SHA-256 checksum file.'
    }
    if ($DownloadOnly) {
        New-Item -ItemType Directory -Path $DownloadDirectory -Force | Out-Null
        foreach ($name in 'xshot.json', $archiveName, $checksumsName) {
            Copy-Item -LiteralPath (Join-Path $stage $name) -Destination (Join-Path $DownloadDirectory $name) -Force
        }
        Write-Output "Verified xshot $releaseVersion from $Repository in $DownloadDirectory."
        return
    }

    $scoopCommand = Get-Command scoop -ErrorAction Stop
    $scoopRoot = if ($env:SCOOP) { $env:SCOOP } else { Join-Path $env:USERPROFILE 'scoop' }
    $bucketName = 'xshot-authenticated'
    $bucketRoot = Join-Path $scoopRoot "buckets\$bucketName\bucket"
    $bucketManifest = Join-Path $bucketRoot 'xshot.json'
    $appRoot = Join-Path $scoopRoot 'apps\xshot'
    $installed = Join-Path $appRoot 'current'
    if (Test-Path -LiteralPath $bucketManifest) {
        $previous = Get-Content -LiteralPath $bucketManifest -Raw | ConvertFrom-Json
        if ($previous.homepage -ne $manifest.homepage) { throw "$bucketName already belongs to a different repository." }
    }
    if (Test-Path -LiteralPath $installed) {
        $info = Get-Content -LiteralPath (Join-Path $installed 'install.json') -Raw | ConvertFrom-Json
        if ($info.bucket -ne $bucketName) {
            throw 'xshot was installed from another Scoop source. Uninstall it before switching to authenticated releases.'
        }
    } elseif (Test-Path -LiteralPath $appRoot) {
        throw 'A previous Scoop xshot installation is incomplete. Run scoop uninstall xshot, then retry.'
    }

    # Use Scoop's own URL-to-cache mapping. The original release URL and hash
    # stay intact in the manifest; Scoop verifies the cached ZIP normally.
    $cachePath = & {
        Set-StrictMode -Off
        . (Join-Path $scoopRoot 'apps\scoop\current\lib\core.ps1')
        cache_path 'xshot' $releaseVersion $expectedUrl
    }
    New-Item -ItemType Directory -Path (Split-Path $cachePath -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $archivePath -Destination $cachePath -Force
    New-Item -ItemType Directory -Path $bucketRoot -Force | Out-Null
    Copy-Item -LiteralPath $manifestPath -Destination $bucketManifest -Force
    $updating = Test-Path -LiteralPath $installed
    if ($updating) {
        & (Join-Path $installed 'Scoop.ps1') -Action Stop
    }
    $scoopExitCode = & {
        Set-StrictMode -Off
        if ($updating) { & $scoopCommand update xshot | Out-Host }
        else { & $scoopCommand install "$bucketName/xshot" | Out-Host }
        $LASTEXITCODE
    }
    if ($scoopExitCode -ne 0) { throw "Scoop failed with exit code $scoopExitCode." }
    if (!(Test-Path -LiteralPath (Join-Path $installed 'VERSION')) -or
        (Get-Content -LiteralPath (Join-Path $installed 'VERSION') -Raw).Trim() -ne $releaseVersion) {
        throw "Scoop did not install xshot $releaseVersion. Review its output and retry."
    }
    # scoop update skips an already-current version after Stop above.
    Start-Process (Join-Path $installed 'xshot.exe') -ArgumentList '--background'
    Write-Output "Installed xshot $releaseVersion through Scoop. Press Ctrl+Print Screen to capture."
    Write-Output 'For future private updates, run Install-Scoop.ps1 again. Uninstall with scoop uninstall xshot.'
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
