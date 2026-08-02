@echo off
chcp 65001 >nul
rem Lyrics Tray Icon Fix v0.37
set "ROOT=%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process PowerShell -Verb RunAs -Wait -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File ""%ROOT%install-startup-admin.ps1""'"
if errorlevel 1 (
  echo Admin startup install failed. Falling back to current-user startup.
  powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%install-task.ps1"
)

wscript.exe "%ROOT%run-hidden.vbs"
"%ROOT%bin\Lyrics Tray Icon Fix.exe" status
pause
