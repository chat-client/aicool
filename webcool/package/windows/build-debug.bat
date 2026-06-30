@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%build-windows.ps1" -Configuration Debug %*
exit /b %ERRORLEVEL%
