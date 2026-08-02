@echo off
chcp 65001 >nul
setlocal
set "ROOT=%~dp0"
set "LAUNCHER=%ROOT%run-hidden.vbs"

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%set-autostart.ps1" "%LAUNCHER%"
if errorlevel 1 (
  echo 开机自启设置失败。
  pause
  exit /b 1
)

wscript.exe "%LAUNCHER%"
if errorlevel 1 (
  echo 服务启动失败。
  pause
  exit /b 1
)

echo 已设置开机自启并启动 Lyrics Tray Icon Fix。
"%ROOT%bin\Lyrics Tray Icon Fix.exe" status
pause
