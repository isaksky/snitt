$ErrorActionPreference = 'Stop'
$destination = Join-Path $env:LOCALAPPDATA 'Programs\snitt'
$exe = Join-Path $destination 'snitt.exe'
$running = @(Get-Process snitt -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
if ($running.Count) {
    $p = Start-Process $exe -ArgumentList '--quit' -PassThru
    if (!$p.WaitForExit(5000)) { throw 'Quit Snitt from its tray menu before uninstalling.' }
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        $running = @(Get-Process snitt -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
        if (!$running.Count) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($running.Count) { throw 'Quit Snitt in each desktop session before uninstalling.' }
}
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if ((Get-ItemProperty $runKey -Name snitt -ErrorAction SilentlyContinue).snitt -eq ('"' + $exe + '" --background')) {
    Remove-ItemProperty $runKey -Name snitt
}
$link = Join-Path ([Environment]::GetFolderPath('Programs')) 'Snitt.lnk'
if (Test-Path $link) {
    $shell = New-Object -ComObject WScript.Shell
    if ($shell.CreateShortcut($link).TargetPath -eq $exe) { Remove-Item -LiteralPath $link }
}
if (Test-Path $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
Write-Output 'Snitt removed.'
