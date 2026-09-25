$ErrorActionPreference = 'Stop'
$destination = Join-Path $env:LOCALAPPDATA 'Programs\xshot'
$exe = Join-Path $destination 'xshot.exe'
$running = @(Get-Process xshot -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
if ($running.Count) {
    $p = Start-Process $exe -ArgumentList '--quit' -PassThru
    if (!$p.WaitForExit(5000)) { throw 'Quit xshot from its tray menu before uninstalling.' }
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        $running = @(Get-Process xshot -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
        if (!$running.Count) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($running.Count) { throw 'Quit xshot in each desktop session before uninstalling.' }
}
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if ((Get-ItemProperty $runKey -Name xshot -ErrorAction SilentlyContinue).xshot -eq ('"' + $exe + '" --background')) {
    Remove-ItemProperty $runKey -Name xshot
}
$link = Join-Path ([Environment]::GetFolderPath('Programs')) 'xshot.lnk'
if (Test-Path $link) {
    $shell = New-Object -ComObject WScript.Shell
    if ($shell.CreateShortcut($link).TargetPath -eq $exe) { Remove-Item -LiteralPath $link }
}
if (Test-Path $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
Write-Output 'xshot removed.'
