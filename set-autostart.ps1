param([string]$Launcher)

$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$wscript = Join-Path $env:WINDIR 'System32\wscript.exe'
$value = '"' + $wscript + '" "' + $Launcher + '"'

New-Item -Path $runKey -Force | Out-Null
Set-ItemProperty -Path $runKey -Name 'Lyrics Tray Icon Fix' -Value $value -Type String
