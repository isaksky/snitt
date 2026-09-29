$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('snitt-auth-test-' + [Guid]::NewGuid().ToString('N'))
$global:snittFixtureDirectory = Join-Path $temporary 'fixture'
$output = Join-Path $temporary 'verified'
New-Item -ItemType Directory -Path $global:snittFixtureDirectory -Force | Out-Null
$global:snittFixtureTag = 'v1.2.3'
$global:snittFixtureDownloadCount = 0
function global:gh {
    $global:LASTEXITCODE = 0
    if ($args[0] -ne 'release') { throw 'The wrapper attempted an unexpected GitHub command.' }
    if ($args[1] -eq 'view') { return $global:snittFixtureTag }
    if ($args[1] -ne 'download') { throw 'The wrapper attempted an unexpected GitHub release command.' }
    $global:snittFixtureDownloadCount++
    $destination = $args[[Array]::IndexOf($args, '--dir') + 1]
    Copy-Item (Join-Path $global:snittFixtureDirectory '*') -Destination $destination
}
try {
    $archiveName = 'snitt_1.2.3_windows_amd64.zip'
    $archive = Join-Path $global:snittFixtureDirectory $archiveName
    [IO.File]::WriteAllBytes($archive, [Text.Encoding]::UTF8.GetBytes('release test fixture'))
    $digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest = @{
        version = '1.2.3'; homepage = 'https://github.com/example/private-snitt'
        bin = 'snitt.exe'; depends = 'main/ffmpeg'
        architecture = @{ '64bit' = @{ hash = $digest; url = "https://github.com/example/private-snitt/releases/download/v1.2.3/$archiveName" } }
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $global:snittFixtureDirectory 'snitt.json')
    "$digest  $archiveName" | Set-Content (Join-Path $global:snittFixtureDirectory 'snitt_1.2.3_checksums.txt')
    $wrapper = Join-Path $root 'platform/windows/Install-Scoop.ps1'
    & $wrapper -Repository example/private-snitt -DownloadOnly -DownloadDirectory $output
    if (!(Test-Path (Join-Path $output $archiveName))) { throw 'A verified download was not copied.' }

    [IO.File]::AppendAllText($archive, 'corrupted')
    $rejected = $false
    try { & $wrapper -Repository example/private-snitt -Version 1.2.3 -DownloadOnly -DownloadDirectory $output }
    catch { if ($_.Exception.Message -notlike '*SHA-256*') { throw }; $rejected = $true }
    if (!$rejected) { throw 'A corrupted ZIP was accepted.' }
    if ((Get-FileHash (Join-Path $output $archiveName) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $digest) { throw 'A corrupted download replaced the verified output.' }

    $global:snittFixtureTag = 'not-a-version'
    $downloads = $global:snittFixtureDownloadCount
    $rejected = $false
    try { & $wrapper -Repository example/private-snitt -DownloadOnly -DownloadDirectory $output }
    catch { if ($_.Exception.Message -notlike '*not a stable Snitt version*') { throw }; $rejected = $true }
    if (!$rejected -or $downloads -ne $global:snittFixtureDownloadCount) { throw 'An invalid release tag reached the downloader.' }
    Write-Output 'Verified authenticated download flow, independent checksums, corruption rejection, and invalid-tag rejection with mocked gh.'
} finally {
    Remove-Item Function:\gh -ErrorAction SilentlyContinue
    Remove-Variable snittFixtureDirectory, snittFixtureTag, snittFixtureDownloadCount -Scope Global -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
