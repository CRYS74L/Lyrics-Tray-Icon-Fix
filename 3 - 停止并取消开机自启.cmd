@echo off
chcp 65001 >nul
setlocal
set "ROOT=%~dp0"

"%ROOT%bin\Lyrics Tray Icon Fix.exe" stop
powershell -NoProfile -ExecutionPolicy Bypass -Command "Remove-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'Lyrics Tray Icon Fix' -Force -ErrorAction SilentlyContinue"
echo 已停止当前服务并取消开机自启。
pause
