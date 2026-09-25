$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
$destination = Join-Path $env:LOCALAPPDATA 'Programs\xshot'
if ([IO.Path]::GetFullPath($source).TrimEnd('\') -eq [IO.Path]::GetFullPath($destination).TrimEnd('\')) {
    throw 'Run Install.cmd from the extracted download, not the installed directory.'
}
$exe = Join-Path $destination 'xshot.exe'
if (Test-Path $exe) {
    $quit = Start-Process $exe -ArgumentList '--quit' -PassThru
    $quit.WaitForExit()
    for ($i = 0; $i -lt 30; $i++) {
        $running = @(Get-Process xshot -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
        if (!$running.Count) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($running.Count) { throw 'Quit xshot from its tray menu, then install again.' }
}
$stage = "$destination-install-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item "$source\*" $stage -Recurse
$backup = "$destination-previous"
if (Test-Path $backup) { throw "Previous installation backup still exists: $backup" }
if (Test-Path $destination) { Move-Item $destination $backup }
Move-Item $stage $destination
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
New-Item -Path $runKey -Force | Out-Null
Set-ItemProperty -Path $runKey -Name 'xshot' -Value ('"' + $exe + '" --background')
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Programs')) 'xshot.lnk'))
$link.TargetPath = $exe
$link.WorkingDirectory = $destination
$link.Description = 'Capture a region with xshot'
$link.Save()
if (Test-Path $backup) { Remove-Item -LiteralPath $backup -Recurse -Force }
Start-Process $exe -ArgumentList '--background'
Write-Output 'xshot is installed and starts at login. Press Ctrl+Print Screen to capture.'
