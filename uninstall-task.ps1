$ErrorActionPreference = 'SilentlyContinue'

$taskName = 'Lyrics Tray Icon Fix'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
Remove-ItemProperty -Path $runKey -Name $taskName -Force
Write-Host "宸插垹闄ゅ綋鍓嶇敤鎴风櫥褰曞惎鍔ㄩ」: $taskName"
