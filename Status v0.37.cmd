@echo off
chcp 65001 >nul
rem Lyrics Tray Icon Fix v0.37
set "ROOT=%~dp0"

"%ROOT%bin\Lyrics Tray Icon Fix.exe" status
echo.
echo Current-user Run entry:
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ItemProperty -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\Run -Name 'Lyrics Tray Icon Fix' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty 'Lyrics Tray Icon Fix'"
echo.
echo Logon and shell-recovery scheduled task:
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ScheduledTask -TaskName 'Lyrics Tray Icon Fix' -ErrorAction SilentlyContinue | Select-Object TaskName,State | Format-Table -AutoSize"
pause
