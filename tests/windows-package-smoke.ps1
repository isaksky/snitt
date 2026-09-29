param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$EditorTests,
    [Parameter(Mandatory=$true)][string]$QtBin,
    [Parameter(Mandatory=$true)][string]$ResultsDirectory
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $ResultsDirectory | Out-Null
foreach ($required in @(
    (Join-Path $PackageDirectory 'snitt.exe'),
    (Join-Path $PackageDirectory 'qt.conf'),
    (Join-Path $PackageDirectory 'qml\QtMultimedia\qmldir'),
    (Join-Path $PackageDirectory 'plugins\multimedia\ffmpegmediaplugin.dll'),
    (Join-Path $PackageDirectory 'licenses\ffmpeg\manifest.json'),
    $EditorTests,
    (Join-Path $QtBin 'Qt6Test.dll')
)) {
    if (!(Test-Path -LiteralPath $required)) { throw "Package smoke prerequisite missing: $required" }
}

# The test executable is a temporary probe beside the packaged application.
# The archive itself stays sealed; Qt discovers only its packaged DLLs, plugins,
# QML imports, and qt.conf. Qt6Test is needed solely by the probe.
$probe = Join-Path $PackageDirectory 'snitt-package-smoke.exe'
$qtTest = Join-Path $PackageDirectory 'Qt6Test.dll'
Copy-Item -LiteralPath $EditorTests -Destination $probe -Force
Copy-Item -LiteralPath (Join-Path $QtBin 'Qt6Test.dll') -Destination $qtTest -Force
$originalPath = $env:PATH
$originalPluginPath = $env:QT_PLUGIN_PATH
$originalQmlPath = $env:QML_IMPORT_PATH
$originalQml2Path = $env:QML2_IMPORT_PATH
$originalPlatform = $env:QT_QPA_PLATFORM
$originalQuickBackend = $env:QT_QUICK_BACKEND
$originalInteractive = $env:SNITT_INTERACTIVE_TESTS
try {
    $env:PATH = "$PackageDirectory;$env:WINDIR\System32;$env:WINDIR;$env:WINDIR\System32\Wbem"
    Remove-Item Env:QT_PLUGIN_PATH,Env:QML_IMPORT_PATH,Env:QML2_IMPORT_PATH,Env:QT_QPA_PLATFORM,Env:QT_QUICK_BACKEND -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath $probe -ArgumentList @('qmlRecordingReview','-o',"$ResultsDirectory\package-review.txt,txt") `
        -WorkingDirectory $PackageDirectory `
        -RedirectStandardOutput (Join-Path $ResultsDirectory 'package-review-out.log') `
        -RedirectStandardError (Join-Path $ResultsDirectory 'package-review-err.log') -PassThru
    $null = $p.Handle
    if (!$p.WaitForExit(120000)) {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        throw 'Packaged playback/trim review timed out'
    }
    if ($p.ExitCode -ne 0) { throw "Packaged playback/trim review exited $($p.ExitCode)" }
    $runtime = Get-Content (Join-Path $PackageDirectory 'licenses\ffmpeg\manifest.json') -Raw | ConvertFrom-Json
    $expectedRuntime = "FFmpeg version $($runtime.ffmpeg_version) LGPL version 2.1 or later"
    if (!(Select-String -LiteralPath (Join-Path $ResultsDirectory 'package-review.txt') -SimpleMatch $expectedRuntime -Quiet)) {
        throw 'Packaged playback did not load the expected LGPL FFmpeg runtime.'
    }
    $env:SNITT_INTERACTIVE_TESTS = '1'
    $controls = Start-Process -FilePath $probe -ArgumentList @('qmlRecordingControls','qmlRecordingHotkeyStop','-o',"$ResultsDirectory\package-controls.txt,txt") `
        -WorkingDirectory $PackageDirectory `
        -RedirectStandardOutput (Join-Path $ResultsDirectory 'package-controls-out.log') `
        -RedirectStandardError (Join-Path $ResultsDirectory 'package-controls-err.log') -PassThru
    $null = $controls.Handle
    if (!$controls.WaitForExit(120000)) {
        Stop-Process -Id $controls.Id -Force -ErrorAction SilentlyContinue
        throw 'Packaged recording controls timed out'
    }
    if ($controls.ExitCode -ne 0) { throw "Packaged recording controls exited $($controls.ExitCode)" }
    'Packaged QtMultimedia review, playback, thumbnails, Save trim, and recording controls passed with system-only PATH.' |
        Set-Content -LiteralPath (Join-Path $ResultsDirectory 'package-smoke.txt')
} finally {
    $env:PATH = $originalPath
    $env:QT_PLUGIN_PATH = $originalPluginPath
    $env:QML_IMPORT_PATH = $originalQmlPath
    $env:QML2_IMPORT_PATH = $originalQml2Path
    $env:QT_QPA_PLATFORM = $originalPlatform
    $env:QT_QUICK_BACKEND = $originalQuickBackend
    $env:SNITT_INTERACTIVE_TESTS = $originalInteractive
    Remove-Item -LiteralPath $probe,$qtTest -Force -ErrorAction SilentlyContinue
}
