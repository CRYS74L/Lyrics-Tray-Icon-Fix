$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$taskName = 'Lyrics Tray Icon Fix'
$launcher = Join-Path $root 'run-hidden.vbs'
$wscript = Join-Path $env:WINDIR 'System32\wscript.exe'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runValue = '"' + $wscript + '" "' + $launcher + '"'

New-Item -Path $runKey -Force | Out-Null
Set-ItemProperty -Path $runKey -Name $taskName -Value $runValue -Type String

$action = New-ScheduledTaskAction -Execute $wscript -Argument "`"$launcher`""
$trigger = New-ScheduledTaskTrigger -AtLogOn
$principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit 0 -MultipleInstances IgnoreNew

Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null

Write-Host "宸插垱寤哄綋鍓嶇敤鎴峰惎鍔ㄩ」鍜岀櫥褰曡鍒掍换鍔? $taskName"
