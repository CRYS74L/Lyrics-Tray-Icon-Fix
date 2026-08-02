$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$taskName = 'Lyrics Tray Icon Fix'
$launcher = Join-Path $root 'run-hidden.vbs'
$wscript = Join-Path $env:WINDIR 'System32\wscript.exe'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runValue = '"' + $wscript + '" "' + $launcher + '"'

New-Item -Path $runKey -Force | Out-Null
Set-ItemProperty -Path $runKey -Name $taskName -Value $runValue -Type String

$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$xmlEscape = { param($value) [Security.SecurityElement]::Escape($value) }
$commandXml = & $xmlEscape $wscript
$argumentsXml = & $xmlEscape ('"' + $launcher + '"')
$subscription = "&lt;QueryList&gt;&lt;Query Id='0' Path='System'&gt;&lt;Select Path='System'&gt;*[System[Provider[@Name='Microsoft-Windows-Winlogon'] and EventID=1002]]&lt;/Select&gt;&lt;/Query&gt;&lt;/QueryList&gt;"
$taskXml = @"
<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo><Description>Start at logon and recover if the Windows shell and controller exit together.</Description></RegistrationInfo>
  <Triggers>
    <LogonTrigger><Enabled>true</Enabled><UserId>$sid</UserId></LogonTrigger>
    <EventTrigger><Enabled>true</Enabled><Subscription>$subscription</Subscription></EventTrigger>
  </Triggers>
  <Principals><Principal id="Author"><UserId>$sid</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>
  <Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><StartWhenAvailable>true</StartWhenAvailable><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Enabled>true</Enabled></Settings>
  <Actions Context="Author"><Exec><Command>$commandXml</Command><Arguments>$argumentsXml</Arguments></Exec></Actions>
</Task>
"@

Register-ScheduledTask -TaskName $taskName -Xml $taskXml -Force | Out-Null

Write-Host "宸插垱寤哄綋鍓嶇敤鎴峰惎鍔ㄩ」鍜岀櫥褰曡鍒掍换鍔? $taskName"
