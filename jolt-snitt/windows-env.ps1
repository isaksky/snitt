# Dot-source after build-native.ps1. Windows has no rpath: the Qt SDK and the
# Glimmer bridge must be on PATH for jolt.exe to load snitt_jolt.dll.
$qmake = (Get-Content (Join-Path $PSScriptRoot '..\glimmer-qt\build\qmake-path')).Trim()
$glimmerBuild = (Resolve-Path (Join-Path $PSScriptRoot '..\glimmer-qt\build')).Path
$env:PATH = "$glimmerBuild;$(Split-Path $qmake -Parent);$env:PATH"
$env:QT_PLUGIN_PATH = (& $qmake -query QT_INSTALL_PLUGINS)
$env:QML_IMPORT_PATH = (& $qmake -query QT_INSTALL_QML)
$env:JOLT_AOT_CACHE = '0'
$jolt = if ($env:JOLT) { $env:JOLT } else { 'jolt' }
