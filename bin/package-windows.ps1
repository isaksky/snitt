param(
    [string]$QtBin = $env:XSHOT_QT_BIN,
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$Version = '0.1.0',
    [string]$ReleaseDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$QtBin) { $QtBin = Join-Path $env:USERPROFILE 'scoop\apps\msys2\current\ucrt64\bin' }
& "$PSScriptRoot\build.ps1" -QtBin $QtBin
$env:PATH = "$QtBin;$env:PATH"
$destination = Join-Path $root 'build\package\windows\xshot'
if (Test-Path $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item "$root\build\windows-app\xshot.exe" $destination
& "$QtBin\windeployqt6.exe" --release --compiler-runtime --no-translations --qmldir "$root\src" --dir $destination --plugindir "$destination\plugins" --qml-deploy-dir "$destination\qml" "$destination\xshot.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
# MSYS2 Qt also links external libraries. Follow PE imports recursively, rather
# than shipping all of MSYS2 or relying on the development machine PATH.
$queue = [Collections.Generic.Queue[string]]::new()
Get-ChildItem $destination -Recurse -File | Where-Object { $_.Extension -in '.exe','.dll' } | ForEach-Object { $queue.Enqueue($_.FullName) }
$seen = @{}
while ($queue.Count) {
    $file = $queue.Dequeue()
    if ($seen.ContainsKey($file)) { continue }
    $seen[$file] = $true
    $imports = & "$QtBin\objdump.exe" -p $file
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect $file" }
    foreach ($line in $imports) {
        if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
        $name = $Matches[1]
        $target = Join-Path $destination $name
        $source = Join-Path $QtBin $name
        if (Test-Path $target) { continue }
        if (Test-Path $source) {
            Copy-Item $source $target
            $queue.Enqueue($target)
        } elseif ($name -notmatch '^(api-ms-|ext-ms-)' -and !(Test-Path "$env:WINDIR\System32\$name")) {
            throw "Unresolved runtime dependency: $name ($file)"
        }
    }
}
"[Paths]`nPrefix=.`nPlugins=plugins`nQmlImports=qml" | Set-Content "$destination\qt.conf" -Encoding ascii
Copy-Item "$root\platform\windows\*" $destination
Copy-Item "$root\README.md" $destination
$Version | Set-Content "$destination\VERSION" -Encoding ascii
$licenses = Join-Path $destination 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
$qtPrefix = Split-Path $QtBin -Parent
if (!(Test-Path "$qtPrefix\share\licenses")) { throw 'MSYS2 dependency license directory is missing.' }
Copy-Item "$qtPrefix\share\licenses\*" $licenses -Recurse
foreach ($required in 'qt6-base\LGPL-3.0-only.txt', 'qt6-base\GPL-3.0-only.txt', 'qt6-declarative\LGPL-3.0-only.txt', 'gcc\COPYING.RUNTIME') {
    if (!(Test-Path (Join-Path $licenses $required))) { throw "Required runtime license is missing: $required" }
}
# ICU stores its notices outside share/licenses in the MSYS2 package.
if (Get-ChildItem $destination -Filter 'libicu*.dll') {
    $icuLicenses = @(Get-ChildItem "$qtPrefix\share\icu\*\LICENSE" -File)
    if (!$icuLicenses.Count) { throw 'ICU runtime DLLs were deployed without their license file.' }
    foreach ($icuLicense in $icuLicenses) {
        $icuDirectory = Join-Path $licenses ('icu-' + $icuLicense.Directory.Name)
        New-Item -ItemType Directory -Force $icuDirectory | Out-Null
        Copy-Item -LiteralPath $icuLicense.FullName -Destination (Join-Path $icuDirectory 'LICENSE')
    }
}
$qtVersion = (& "$QtBin\qmake6.exe" -query QT_VERSION).Trim()
$qtSeries = ($qtVersion -split '\.')[0..1] -join '.'
@"
This archive includes dynamically linked Qt $qtVersion libraries and plugins
and their MSYS2 runtime dependencies. Their license texts and notices are in
licenses/. These licenses apply to the named components, not to xshot itself.
The original DLLs remain separate and can be replaced by compatible builds.

Qt source:
https://download.qt.io/archive/qt/$qtSeries/$qtVersion/submodules/qtbase-everywhere-src-$qtVersion.tar.xz
https://download.qt.io/archive/qt/$qtSeries/$qtVersion/submodules/qtdeclarative-everywhere-src-$qtVersion.tar.xz
Qt third-party component attribution:
https://doc.qt.io/qt-6/licenses-used-in-qt.html
MSYS2 package recipes and patches:
https://github.com/msys2/MINGW-packages
https://packages.msys2.org/

FFmpeg is a separate command-line dependency, installed by Scoop. It is not
included in this archive. See its installation for its licenses and notices.
"@ | Set-Content "$destination\THIRD-PARTY-NOTICES.txt" -Encoding utf8
if (!$ReleaseDirectory) { $ReleaseDirectory = Join-Path $root 'build\release' }
New-Item -ItemType Directory -Force $ReleaseDirectory | Out-Null
$archiveName = "xshot_${Version}_windows_amd64.zip"
$archive = Join-Path $ReleaseDirectory $archiveName
if (Test-Path $archive) { Remove-Item -LiteralPath $archive }
Compress-Archive -Path "$destination\*" -DestinationPath $archive
$digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$digest  $archiveName" | Set-Content (Join-Path $ReleaseDirectory "xshot_${Version}_checksums.txt") -Encoding ascii
Write-Output "Packaged $archive"
Write-Output "SHA-256 $digest"
