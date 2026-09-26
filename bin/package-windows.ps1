param(
    [string]$QtBin = $env:XSHOT_QT_BIN,
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$Version = '0.1.0',
    [string]$ReleaseDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$QtBin) { $QtBin = Join-Path $env:USERPROFILE 'scoop\apps\msys2\current\ucrt64\bin' }
$qtPrefix = Split-Path $QtBin -Parent
& "$PSScriptRoot\build.ps1" -QtBin $QtBin
$env:PATH = "$QtBin;$env:PATH"
$destination = Join-Path $root 'build\package\windows\xshot'
if (Test-Path $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item "$root\build\windows-app\xshot.exe" $destination
& "$QtBin\windeployqt6.exe" --release --compiler-runtime --no-translations --qmldir "$root\src" --dir $destination --plugindir "$destination\plugins" --qml-deploy-dir "$destination\qml" "$destination\xshot.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
if (!(Test-Path (Join-Path $destination 'plugins\imageformats\qsvg.dll'))) { throw 'Bundled SVG image plugin is missing.' }
foreach ($qmlFile in 'qmldir','quickmultimediaplugin.dll') {
    if (!(Test-Path (Join-Path $destination "qml\QtMultimedia\$qmlFile"))) {
        throw "Bundled QtMultimedia QML import is missing $qmlFile."
    }
}
# The media backend is a separate MSYS2 package. windeployqt can discover the
# QML module without copying this plugin, so include it explicitly before the
# recursive PE walk gathers its codec DLL dependencies.
$mediaBackendSource = Join-Path $qtPrefix 'share\qt6\plugins\multimedia\ffmpegmediaplugin.dll'
if (!(Test-Path $mediaBackendSource)) { throw 'MSYS2 QtMultimedia FFmpeg backend is missing.' }
$mediaBackendDirectory = Join-Path $destination 'plugins\multimedia'
New-Item -ItemType Directory -Force $mediaBackendDirectory | Out-Null
Copy-Item -LiteralPath $mediaBackendSource -Destination (Join-Path $mediaBackendDirectory 'ffmpegmediaplugin.dll') -Force
# Some backend builds load codec libraries dynamically rather than listing all
# of them in PE imports. Keep the required playback libraries in the archive.
$codecDlls = @()
foreach ($library in 'avcodec','avformat','avutil','swresample','swscale') {
    $candidates = @(Get-ChildItem -LiteralPath $QtBin -Filter "$library-*.dll" -File)
    if ($candidates.Count -ne 1) { throw "Expected one MSYS2 $library codec DLL, found $($candidates.Count)." }
    $codecDlls += $candidates[0].Name
    Copy-Item -LiteralPath $candidates[0].FullName -Destination (Join-Path $destination $candidates[0].Name) -Force
}
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
if (!(Test-Path "$qtPrefix\share\licenses")) { throw 'MSYS2 dependency license directory is missing.' }
Copy-Item "$qtPrefix\share\licenses\*" $licenses -Recurse
New-Item -ItemType Directory -Force (Join-Path $licenses 'lucide') | Out-Null
Copy-Item "$root\src\icons\LICENSE" (Join-Path $licenses 'lucide\LICENSE')
New-Item -ItemType Directory -Force (Join-Path $licenses 'omacut') | Out-Null
Copy-Item "$root\src\OMACUT-LICENSE" (Join-Path $licenses 'omacut\LICENSE')
foreach ($required in 'qt6-base\LGPL-3.0-only.txt', 'qt6-base\GPL-3.0-only.txt', 'qt6-declarative\LGPL-3.0-only.txt', 'qt6-multimedia\LGPL-3.0-only.txt', 'qt6-multimedia\GPL-3.0-only.txt', 'gcc\COPYING.RUNTIME') {
    if (!(Test-Path (Join-Path $licenses $required))) { throw "Required runtime license is missing: $required" }
}
# The MSYS2 FFmpeg binary package identifies itself as GPL-3.0-or-later but
# does not install a separate license file. Include the standard GPLv3 text
# already supplied by Qt's MSYS2 package and identify FFmpeg's terms below.
$ffmpegLicenses = Join-Path $licenses 'ffmpeg'
New-Item -ItemType Directory -Force $ffmpegLicenses | Out-Null
Copy-Item -LiteralPath (Join-Path $licenses 'qt6-multimedia\GPL-3.0-only.txt') -Destination (Join-Path $ffmpegLicenses 'GPL-3.0.txt')
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
https://download.qt.io/archive/qt/$qtSeries/$qtVersion/submodules/qtmultimedia-everywhere-src-$qtVersion.tar.xz
Qt third-party component attribution:
https://doc.qt.io/qt-6/licenses-used-in-qt.html
MSYS2 package recipes and patches:
https://github.com/msys2/MINGW-packages
https://packages.msys2.org/

Lucide SVG icons are bundled in xshot under the Lucide/Feather terms in
licenses/lucide/LICENSE. Source: https://github.com/lucide-icons/lucide

The recording trim filmstrip and time formatter are adapted from omacut under
the MIT license in licenses/omacut/LICENSE. Source: https://github.com/omacom/omacut

QtMultimedia playback uses the bundled FFmpeg backend plugin and MSYS2 codec
libraries ($($codecDlls -join ', ')). The MSYS2 FFmpeg package identifies these
libraries as GPL-3.0-or-later. The GPLv3 text is in licenses/ffmpeg/GPL-3.0.txt;
source package and build details: https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-ffmpeg
The separate ffmpeg.exe command-line tool is not included in this archive.
Recording and trimming need that executable installed through Scoop; its own
distribution supplies its applicable licenses and notices.
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
