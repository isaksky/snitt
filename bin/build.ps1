param([string]$QtBin = $env:XSHOT_QT_BIN, [switch]$Test)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$QtBin) {
    $scoopQt = Join-Path $env:USERPROFILE 'scoop\apps\msys2\current\ucrt64\bin'
    if (Test-Path "$scoopQt\qmake6.exe") { $QtBin = $scoopQt }
    else {
        $qmake = Get-Command qmake6.exe -ErrorAction SilentlyContinue
        if (!$qmake) { $qmake = Get-Command qmake.exe -ErrorAction Stop }
        $QtBin = Split-Path $qmake.Source -Parent
    }
}
$env:PATH = "$QtBin;$env:PATH"
$qmake = Join-Path $QtBin 'qmake6.exe'
if (!(Test-Path $qmake)) { $qmake = Join-Path $QtBin 'qmake.exe' }
if ((& $qmake -query QT_VERSION) -notmatch '^6\.') { throw 'Qt 6 is required' }
$make = (Get-Command mingw32-make.exe -ErrorAction Stop).Source
$build = Join-Path $root 'build\windows-app'
New-Item -ItemType Directory -Force $build | Out-Null
Push-Location $build
try {
    & $qmake "$root\xshot.pro" 'CONFIG+=release' 'CONFIG-=debug_and_release'
    if ($LASTEXITCODE -ne 0) { throw 'qmake failed' }
    & $make '-j4'
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
} finally { Pop-Location }
if ($Test) {
    $testDir = Join-Path $build 'tests'
    New-Item -ItemType Directory -Force $testDir | Out-Null
    Push-Location $testDir
    $oldPlatform = $env:QT_QPA_PLATFORM
    try {
        & $qmake "$root\tests\editor_tests.pro" 'CONFIG+=release' 'CONFIG-=debug_and_release'
        if ($LASTEXITCODE -ne 0) { throw 'Test qmake failed' }
        & $make '-j4'
        if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
        $env:QT_QPA_PLATFORM = 'offscreen'
        & '.\editor_tests.exe' '-o' 'results.txt,txt' '-o' 'results.xml,junitxml'
        $code = $LASTEXITCODE
        Get-Content results.txt
        if ($code -ne 0) { throw "Tests failed: $code" }
    } finally { $env:QT_QPA_PLATFORM = $oldPlatform; Pop-Location }
}
Write-Output "Built $build\xshot.exe"
