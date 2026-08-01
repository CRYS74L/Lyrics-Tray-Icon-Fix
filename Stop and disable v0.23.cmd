@echo off
chcp 65001 >nul
set "ROOT=%~dp0"

"%ROOT%bin\Lyrics Tray Icon Fix.exe" stop
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process PowerShell -Verb RunAs -Wait -ArgumentList '-NoProfile -ExecutionPolicy Bypass -Command ""Unregister-ScheduledTask -TaskName ''Lyrics Tray Icon Fix'' -Confirm:$false -ErrorAction SilentlyContinue; Remove-ItemProperty -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\Run -Name ''Lyrics Tray Icon Fix'' -Force -ErrorAction SilentlyContinue""'"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%uninstall-task.ps1"
"%ROOT%bin\Lyrics Tray Icon Fix.exe" status
pause
