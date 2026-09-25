$ErrorActionPreference = 'Stop'
$destination = Join-Path $env:LOCALAPPDATA 'Programs\xshot'
$exe = Join-Path $destination 'xshot.exe'
if (Test-Path $exe) {
    $p = Start-Process $exe -ArgumentList '--quit' -PassThru
    $p.WaitForExit()
    Start-Sleep -Milliseconds 500
}
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if ((Get-ItemProperty $runKey -Name xshot -ErrorAction SilentlyContinue).xshot -eq ('"' + $exe + '" --background')) {
    Remove-ItemProperty $runKey -Name xshot
}
$link = Join-Path ([Environment]::GetFolderPath('Programs')) 'xshot.lnk'
if (Test-Path $link) { Remove-Item -LiteralPath $link }
if (Test-Path $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
Write-Output 'xshot removed.'
