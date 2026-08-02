@echo off
chcp 65001 >nul
setlocal
set "ROOT=%~dp0"

"%ROOT%bin\Lyrics Tray Icon Fix.exe" status
reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "Lyrics Tray Icon Fix" >nul 2>&1
if errorlevel 1 (
  echo 开机自启状态：未开启
) else (
  echo 开机自启状态：已开启
)
pause
