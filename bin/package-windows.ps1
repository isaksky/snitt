param(
    [string]$QtBin = $env:SNITT_QT_BIN,
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$Version = '0.1.0',
    [string]$ReleaseDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$QtBin) { $QtBin = Join-Path $env:USERPROFILE 'scoop\apps\msys2\current\ucrt64\bin' }
$qtPrefix = Split-Path $QtBin -Parent
$python = Join-Path $QtBin 'python.exe'
if (!(Test-Path $python)) { throw 'MSYS2 UCRT64 Python is required to build the private LGPL multimedia runtime.' }
$runtime = Join-Path $root 'build\tools\windows-multimedia-runtime\stage'
& $python "$PSScriptRoot\build-windows-multimedia-runtime.py" --qt-bin $QtBin
if ($LASTEXITCODE -ne 0) { throw 'Private LGPL multimedia runtime build failed.' }
$runtimeManifest = Get-Content (Join-Path $runtime 'manifest.json') -Raw | ConvertFrom-Json
& "$PSScriptRoot\build.ps1" -QtBin $QtBin
$env:PATH = "$QtBin;$env:PATH"
$destination = Join-Path $root 'build\package\windows\snitt'
if (Test-Path $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item "$root\build\windows-app\snitt.exe" $destination
& "$QtBin\windeployqt6.exe" --release --compiler-runtime --no-translations --no-ffmpeg --skip-plugin-types multimedia --qmldir "$root\src" --dir $destination --plugindir "$destination\plugins" --qml-deploy-dir "$destination\qml" "$destination\snitt.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
if (!(Test-Path (Join-Path $destination 'plugins\imageformats\qsvg.dll'))) { throw 'Bundled SVG image plugin is missing.' }
foreach ($qmlFile in 'qmldir','quickmultimediaplugin.dll') {
    if (!(Test-Path (Join-Path $destination "qml\QtMultimedia\$qmlFile"))) {
        throw "Bundled QtMultimedia QML import is missing $qmlFile."
    }
}
# Reuse the Qt plugin with ABI-matched, privately built LGPL FFmpeg DLLs.
# windeployqt must not deploy the SDK's GPL-configured codec libraries.
$mediaBackendSource = Join-Path $qtPrefix 'share\qt6\plugins\multimedia\ffmpegmediaplugin.dll'
if (!(Test-Path $mediaBackendSource)) { throw 'MSYS2 QtMultimedia FFmpeg backend is missing.' }
$mediaBackendDirectory = Join-Path $destination 'plugins\multimedia'
New-Item -ItemType Directory -Force $mediaBackendDirectory | Out-Null
Copy-Item -LiteralPath $mediaBackendSource -Destination (Join-Path $mediaBackendDirectory 'ffmpegmediaplugin.dll') -Force
$codecDlls = @($runtimeManifest.libraries.PSObject.Properties.Name)
foreach ($library in $codecDlls) {
    Copy-Item -LiteralPath (Join-Path $runtime $library) -Destination (Join-Path $destination $library) -Force
}
# MSYS2 Qt also links external libraries. Follow PE imports recursively, rather
# than shipping all of MSYS2 or relying on the development machine PATH.
$queue = [Collections.Generic.Queue[string]]::new()
Get-ChildItem $destination -Recurse -File | Where-Object { $_.Extension -in '.exe','.dll' } | ForEach-Object { $queue.Enqueue($_.FullName) }
$seen = @{}
$dependencyInventory = [ordered]@{}
while ($queue.Count) {
    $file = $queue.Dequeue()
    if ($seen.ContainsKey($file)) { continue }
    $seen[$file] = $true
    $imports = & "$QtBin\objdump.exe" -p $file
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect $file" }
    $fileImports = @()
    foreach ($line in $imports) {
        if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
        $name = $Matches[1]
        $fileImports += $name
        $target = Join-Path $destination $name
        $source = Join-Path $QtBin $name
        if (Test-Path $target) { continue }
        if ($name -match '^(lib)?(avcodec|avformat|avutil|swresample|swscale|avfilter|avdevice|postproc)[-\d.]') {
            throw "Refusing to fill a missing private codec DLL from the SDK: $name ($file)"
        }
        if (Test-Path $source) {
            Copy-Item $source $target
            $queue.Enqueue($target)
        } elseif ($name -notmatch '^(api-ms-|ext-ms-)' -and !(Test-Path "$env:WINDIR\System32\$name")) {
            throw "Unresolved runtime dependency: $name ($file)"
        }
    }
    $relative = $file.Substring($destination.Length + 1).Replace('\','/')
    $dependencyInventory[$relative] = @($fileImports | Sort-Object -Unique)
}
# Prove that dependency deployment neither replaced private DLLs nor added a
# second FFmpeg build. Existing vendor/Qt licenses still apply independently.
foreach ($library in $runtimeManifest.libraries.PSObject.Properties) {
    $actual = (Get-FileHash -LiteralPath (Join-Path $destination $library.Name) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $library.Value.sha256) { throw "Packaged private codec changed: $($library.Name)" }
}
$pluginHash = (Get-FileHash -LiteralPath (Join-Path $mediaBackendDirectory 'ffmpegmediaplugin.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
if ($pluginHash -ne $runtimeManifest.qt_plugin_sha256) { throw 'Packaged Qt playback plugin differs from the validated plugin.' }
foreach ($file in Get-ChildItem $destination -Recurse -Filter '*.dll' -File) {
    if ($file.Name -match '^(lib)?(avcodec|avformat|avutil|swresample|swscale|avfilter|avdevice|postproc)[-\d.]' -and
        ($file.Name -notin $codecDlls -or $file.DirectoryName -ne $destination)) {
        throw "Unexpected FFmpeg library in package: $($file.FullName)"
    }
    if ($file.Name -match '^(lib)?(x264|x265|xvid|vidstab)[-\d.]') {
        throw "Unexpected GPL codec dependency in package: $($file.FullName)"
    }
}
$dependencyInventory | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $destination 'RUNTIME-DEPENDENCIES.json') -Encoding utf8
"[Paths]`nPrefix=.`nPlugins=plugins`nQmlImports=qml" | Set-Content "$destination\qt.conf" -Encoding ascii
Copy-Item "$root\platform\windows\*" $destination
Copy-Item "$root\README.md" $destination
Copy-Item "$root\LICENSE" $destination
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
$ffmpegLicenses = Join-Path $licenses 'ffmpeg'
New-Item -ItemType Directory -Force $ffmpegLicenses | Out-Null
Copy-Item "$runtime\licenses\*" $ffmpegLicenses
Copy-Item -LiteralPath (Join-Path $runtime 'sources') -Destination $ffmpegLicenses -Recurse
Copy-Item -LiteralPath (Join-Path $runtime 'manifest.json') -Destination $ffmpegLicenses
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
licenses/. These licenses apply to the named components, not to Snitt itself.
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

Lucide SVG icons are bundled in Snitt under the Lucide/Feather terms in
licenses/lucide/LICENSE. Source: https://github.com/lucide-icons/lucide

The recording trim filmstrip and time formatter are adapted from omacut under
the MIT license in licenses/omacut/LICENSE. Source: https://github.com/omacom/omacut

Snitt's original code is MIT licensed; see LICENSE.

QtMultimedia playback uses the Qt backend plugin with privately built FFmpeg
$($runtimeManifest.ffmpeg_version) libraries ($($codecDlls -join ', ')), licensed
under LGPL-2.1-or-later. GPL, nonfree, and external codec autodetection are
disabled. The exact FFmpeg source archive and build instructions are included
in licenses/ffmpeg/sources; the LGPL text is licenses/ffmpeg/COPYING.LGPLv2.1.
DLL hashes, compiler details, and configure flags are in licenses/ffmpeg/manifest.json.
These shared libraries may be replaced with compatible modified builds.
The separate ffmpeg.exe command-line tool is not included in this archive.
Recording and trimming need that executable installed through Scoop; its own
distribution supplies its applicable licenses and notices.
"@ | Set-Content "$destination\THIRD-PARTY-NOTICES.txt" -Encoding utf8
if (!$ReleaseDirectory) { $ReleaseDirectory = Join-Path $root 'build\release' }
New-Item -ItemType Directory -Force $ReleaseDirectory | Out-Null
$archiveName = "snitt_${Version}_windows_amd64.zip"
$archive = Join-Path $ReleaseDirectory $archiveName
if (Test-Path $archive) { Remove-Item -LiteralPath $archive }
Compress-Archive -Path "$destination\*" -DestinationPath $archive
$digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$digest  $archiveName" | Set-Content (Join-Path $ReleaseDirectory "snitt_${Version}_checksums.txt") -Encoding ascii
Write-Output "Packaged $archive"
Write-Output "SHA-256 $digest"
