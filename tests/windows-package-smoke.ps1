param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][string]$EditorTests,
    [Parameter(Mandatory=$true)][string]$QtBin,
    [Parameter(Mandatory=$true)][string]$ResultsDirectory
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $ResultsDirectory | Out-Null
foreach ($required in @(
    (Join-Path $PackageDirectory 'xshot.exe'),
    (Join-Path $PackageDirectory 'qt.conf'),
    (Join-Path $PackageDirectory 'qml\QtMultimedia\qmldir'),
    (Join-Path $PackageDirectory 'plugins\multimedia\ffmpegmediaplugin.dll'),
    $EditorTests,
    (Join-Path $QtBin 'Qt6Test.dll')
)) {
    if (!(Test-Path -LiteralPath $required)) { throw "Package smoke prerequisite missing: $required" }
}

# The test executable is a temporary probe beside the packaged application.
# The archive itself stays sealed; Qt discovers only its packaged DLLs, plugins,
# QML imports, and qt.conf. Qt6Test is needed solely by the probe.
$probe = Join-Path $PackageDirectory 'xshot-package-smoke.exe'
$qtTest = Join-Path $PackageDirectory 'Qt6Test.dll'
Copy-Item -LiteralPath $EditorTests -Destination $probe -Force
Copy-Item -LiteralPath (Join-Path $QtBin 'Qt6Test.dll') -Destination $qtTest -Force
$originalPath = $env:PATH
$originalPluginPath = $env:QT_PLUGIN_PATH
$originalQmlPath = $env:QML_IMPORT_PATH
$originalQml2Path = $env:QML2_IMPORT_PATH
$originalPlatform = $env:QT_QPA_PLATFORM
$originalInteractive = $env:XSHOT_INTERACTIVE_TESTS
try {
    $env:PATH = "$PackageDirectory;$env:WINDIR\System32;$env:WINDIR;$env:WINDIR\System32\Wbem"
    Remove-Item Env:QT_PLUGIN_PATH,Env:QML_IMPORT_PATH,Env:QML2_IMPORT_PATH,Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
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
    $env:XSHOT_INTERACTIVE_TESTS = '1'
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
    $env:XSHOT_INTERACTIVE_TESTS = $originalInteractive
    Remove-Item -LiteralPath $probe,$qtTest -Force -ErrorAction SilentlyContinue
}
