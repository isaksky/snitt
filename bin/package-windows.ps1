param([string]$QtBin = $env:XSHOT_QT_BIN)
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
$licenses = Join-Path $destination 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
$qtPrefix = Split-Path $QtBin -Parent
if (Test-Path "$qtPrefix\share\licenses") { Copy-Item "$qtPrefix\share\licenses\*" $licenses -Recurse }
$archive = Join-Path (Split-Path $destination -Parent) 'xshot-windows-x64.zip'
if (Test-Path $archive) { Remove-Item -LiteralPath $archive }
Compress-Archive -Path "$destination\*" -DestinationPath $archive
Write-Output "Packaged $archive"
