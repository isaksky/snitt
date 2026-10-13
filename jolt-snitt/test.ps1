# Windows counterpart of test-app and the offscreen test-* scripts. -Desktop
# runs the real tray, capture, recording and review checks instead; run it in an
# unlocked, signed-in desktop session (not an SSH or service session).
param([switch]$Desktop)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build-native.ps1') 'CONFIG+=property_tests'
. (Join-Path $PSScriptRoot 'windows-env.ps1')
$python = (Get-Command python3.exe -ErrorAction Stop).Source
$build = (Resolve-Path (Join-Path $PSScriptRoot '..\build\jolt-snitt')).Path
$temporary = Join-Path ([IO.Path]::GetTempPath()) ("snitt-jolt-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))

# Native commands do not throw; check each exit code like `set -e`.
function Assert-Success([string]$Step) {
    if ($LASTEXITCODE -ne 0) { throw "$Step exited $LASTEXITCODE" }
}
function Set-Variables([hashtable]$Values) {
    $saved = @{}
    foreach ($key in $Values.Keys) {
        $saved[$key] = [Environment]::GetEnvironmentVariable($key)
        [Environment]::SetEnvironmentVariable($key, $Values[$key])
    }
    return $saved
}
# Runs a block with a fresh data directory and temporary environment values.
function Invoke-Suite([string]$Name, [hashtable]$Environment, [scriptblock]$Body) {
    $data = Join-Path $temporary $Name
    New-Item -ItemType Directory -Force $data | Out-Null
    $values = @{ SNITT_JOLT_DATA_DIR = $data; SNITT_JOLT_FAKE_HOTKEY = '1'; QT_QPA_PLATFORM = 'offscreen' }
    foreach ($key in $Environment.Keys) { $values[$key] = $Environment[$key] }
    $saved = Set-Variables $values
    try { & $Body $data } finally { Set-Variables $saved | Out-Null }
}

Push-Location $PSScriptRoot
try {
    if ($Desktop) {
        $windows = @{ QT_QPA_PLATFORM = 'windows' }
        Invoke-Suite 'desktop' $windows { & $jolt -M:desktop; Assert-Success 'desktop' }
        Invoke-Suite 'capture-desktop' @{ QT_QPA_PLATFORM = 'windows'; SNITT_JOLT_FAKE_CAPTURE = '1'
                                          SNITT_JOLT_IPC_ID = "capture-desktop-$PID" } {
            & $jolt -M:capture-desktop; Assert-Success 'capture-desktop'
        }
        Invoke-Suite 'recording-desktop' $windows {
            param($data)
            $receipt = Join-Path $data 'recording-path.txt'
            $saved = Set-Variables @{ SNITT_JOLT_RECORDING_PATH = $receipt; SNITT_JOLT_FAKE_RECORDER = $null }
            try { & $jolt -M:recording-desktop; Assert-Success 'recording-desktop' } finally { Set-Variables $saved | Out-Null }
            $path = (Resolve-Path (Get-Content $receipt -Raw).Trim()).Path
            $outputs = @(Get-ChildItem $data -Recurse -Filter '*.mp4' | ForEach-Object FullName)
            if ($outputs.Count -ne 1 -or $outputs[0] -ne $path) { throw "Unexpected recording outputs: $outputs; saved $path" }
            $info = & ffprobe -v error -show_entries stream=codec_name,width,height -show_entries format=duration -of json $path |
                Out-String | ConvertFrom-Json
            Assert-Success 'ffprobe'
            $streams = @($info.streams)
            if ($streams.Count -ne 1 -or $streams[0].codec_name -ne 'h264' -or $streams[0].width -ne 320 -or
                $streams[0].height -ne 200 -or [double]$info.format.duration -le 0) {
                throw "Unexpected recording: $($info | ConvertTo-Json -Depth 4 -Compress)"
            }
            & ffmpeg -v error -i $path -f null -; Assert-Success 'recording decode'
            'PASS: valid 320x200 H.264 recording; canceled output removed'
        }
        Invoke-Suite 'review' @{ QT_QPA_PLATFORM = 'windows'; QT_MEDIA_BACKEND = 'ffmpeg' } {
            param($data)
            foreach ($clip in @(@('regular', 'testsrc2=size=320x180:rate=30:duration=1.2', @()),
                                @('short', 'testsrc2=size=320x180:rate=30:duration=0.24', @()),
                                @('static', 'color=c=red:s=160x90:r=1', @('-t', '3')))) {
                & ffmpeg -hide_banner -loglevel error -y -f lavfi -i $clip[1] @($clip[2]) -c:v libx264 -pix_fmt yuv420p "$data\$($clip[0]).mp4"
                Assert-Success "ffmpeg $($clip[0])"
            }
            Copy-Item "$data\regular.mp4" "$data\export.mp4"
            Copy-Item "$data\regular.mp4" "$data\cancel-active.mp4"
            $original = (Get-FileHash "$data\cancel-active.mp4").Hash
            foreach ($case in @(@('regular', @{ SNITT_REVIEW_SHORTCUTS = '1' }), @('short', @{}), @('static', @{}),
                                @('export', @{ SNITT_REVIEW_EXPORT = '1' }), @('cancel-active', @{ SNITT_REVIEW_CANCEL_BUSY = '1' }))) {
                $saved = Set-Variables $case[1]
                try { & $jolt -M:review "$data\$($case[0]).mp4" "$data\missing.mp4"; Assert-Success "review $($case[0])" }
                finally { Set-Variables $saved | Out-Null }
            }
            $duration = [double](& ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$data\export.mp4")
            Assert-Success 'ffprobe'
            if ($duration -lt 0.30 -or $duration -gt 0.50) { throw "Unexpected trimmed duration: $duration" }
            if ((Get-FileHash "$data\cancel-active.mp4").Hash -ne $original) { throw 'Canceled export changed the original.' }
            if (Get-ChildItem $data -Filter '.snitt-trim-*.mp4') { throw 'Temporary trim output remains.' }
            'PASS: trimmed file verified; canceled export left original unchanged'
        }
        return
    }

    & $jolt -M:commands; Assert-Success 'commands'
    & $jolt -M:lifecycle; Assert-Success 'lifecycle'
    Invoke-Suite 'app' @{ SNITT_JOLT_FAKE_CAPTURE = '1' } {
        param($data)
        $image = "$data\source.png"
        $saved = Set-Variables @{ SNITT_JOLT_IMAGE = $image; SNITT_JOLT_SAVED_PATH = "$data\saved-path.txt"
                                  SNITT_JOLT_COPIED_PATH = "$data\copied.png"; SNITT_JOLT_CAPTURE_FILE = "$data\capture.png" }
        try {
            & $python test\image_pixels.py generate $image; Assert-Success 'generate image'
            Copy-Item $image "$data\capture.png"
            & $jolt -M:test; Assert-Success 'app'
            & $jolt -M:capture; Assert-Success 'capture'
            foreach ($mode in 'manual', 'protection-failure') {
                $env:SNITT_JOLT_FAKE_RECORDER = $mode
                try { & $jolt -M:recording; Assert-Success "recording $mode" } finally { $env:SNITT_JOLT_FAKE_RECORDER = $null }
            }
            $scaled = Set-Variables @{ SNITT_JOLT_CAPTURE_DIR = $build; QT_SCALE_FACTOR = '2' }
            try { & $jolt -M:annotation; Assert-Success 'annotation' } finally { Set-Variables $scaled | Out-Null }
            $output = (Get-Content "$data\saved-path.txt" -Raw).Trim()
            & $python test\image_pixels.py verify $output; Assert-Success 'verify saved image'
            & $python test\image_pixels.py verify "$data\copied.png"; Assert-Success 'verify copied image'
            & $python test\image_pixels.py compare $output "$data\copied.png"; Assert-Success 'compare saved and copied'
        } finally { Set-Variables $saved | Out-Null }
    }
    Invoke-Suite 'settings' @{} { & $jolt -M:settings; Assert-Success 'settings' }
    Invoke-Suite 'region' @{ SNITT_JOLT_FAKE_CAPTURE = '1' } {
        param($data)
        $env:SNITT_JOLT_IMAGE = "$data\source.png"
        try {
            & $python test\image_pixels.py generate $env:SNITT_JOLT_IMAGE; Assert-Success 'generate image'
            & $jolt -A:test -m snitt.region-test; Assert-Success 'region'
        } finally { $env:SNITT_JOLT_IMAGE = $null }
    }
    Invoke-Suite 'arrangement' @{} {
        param($data)
        & $python test\arrangement_images.py $data; Assert-Success 'generate arrangement images'
        & $jolt -M:arrangement; Assert-Success 'arrangement'
    }
    Invoke-Suite 'gesture' @{ QT_SCALE_FACTOR = '2' } {
        param($data)
        $env:SNITT_JOLT_IMAGE = "$data\source.png"
        try {
            & $python test\image_pixels.py generate $env:SNITT_JOLT_IMAGE; Assert-Success 'generate image'
            & $jolt -M:gesture; Assert-Success 'gesture'
        } finally { $env:SNITT_JOLT_IMAGE = $null }
        foreach ($migrated in Get-ChildItem $data -Filter 'migrated-*.png') {
            & $python test\image_pixels.py compare $migrated.FullName (Join-Path $data ('reference-' + $migrated.Name.Substring(9)))
            Assert-Success "compare $($migrated.Name)"
        }
    }
    # Qt splits platform options at ':', so the screen configuration path must be relative.
    Invoke-Suite 'preview' @{ QT_QPA_PLATFORM = 'offscreen:configfile=../tests/editor-screens.json' } {
        & $jolt -M:geometry; Assert-Success 'geometry'
        foreach ($ratio in '1', '1.5', '2') {
            $directory = Join-Path $build "preview-${ratio}x"
            New-Item -ItemType Directory -Force $directory | Out-Null
            $saved = Set-Variables @{ QT_SCALE_FACTOR = $ratio; SNITT_PREVIEW_DIR = $directory }
            try {
                & $python test\preview_pixels.py generate $directory; Assert-Success 'generate preview images'
                & $jolt -M:preview; Assert-Success "preview ${ratio}x"
                & $python test\preview_pixels.py verify $directory; Assert-Success "verify preview ${ratio}x"
            } finally { Set-Variables $saved | Out-Null }
        }
    }
    Invoke-Suite 'document' @{ SNITT_JOLT_DOCUMENT_TEST = '1' } {
        param($data)
        & $python test\document_pixels.py generate $data; Assert-Success 'generate document images'
        & $jolt -M:document; Assert-Success 'document'
        & $python test\document_pixels.py compare $data; Assert-Success 'compare document images'
    }
    Invoke-Suite 'export' @{} {
        param($data)
        & $python test\document_pixels.py generate $data; Assert-Success 'generate document images'
        & $jolt -M:export; Assert-Success 'export'
    }
    Invoke-Suite 'trim' @{ JOLT = $jolt } { & $python test\trim_fixtures.py; Assert-Success 'trim' }
    # Last: building the executables rebuilds the native libraries without test hooks.
    Invoke-Suite 'release' @{} {
        param($data)
        $env:SNITT_JOLT_IMAGE = "$data\source.png"
        try {
            & $python test\image_pixels.py generate $env:SNITT_JOLT_IMAGE; Assert-Success 'generate image'
            & (Join-Path $PSScriptRoot 'build-app.ps1')
            & "$build\snitt-jolt-dev.exe" --smoke-test; Assert-Success 'development smoke'
            & "$build\snitt-jolt.exe" --smoke-test; Assert-Success 'release smoke'
            & $python test\lifecycle_processes.py "$build\snitt-jolt.exe" $env:SNITT_JOLT_IMAGE; Assert-Success 'lifecycle processes'
        } finally { $env:SNITT_JOLT_IMAGE = $null }
    }
} finally {
    Pop-Location
    if (Test-Path $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
