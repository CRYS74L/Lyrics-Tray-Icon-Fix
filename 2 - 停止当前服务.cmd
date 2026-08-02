@echo off
chcp 65001 >nul
setlocal
set "ROOT=%~dp0"

"%ROOT%bin\Lyrics Tray Icon Fix.exe" stop
echo 已停止当前 Lyrics Tray Icon Fix 服务。
pause
