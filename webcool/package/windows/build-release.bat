@echo off
setlocal

set "SCRIPT_DIR=%~dp0"

if /i "%~1"=="--help" goto :usage
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage

call "%SCRIPT_DIR%build-windows.bat" -Configuration Release %*
exit /b %ERRORLEVEL%

:usage
call "%SCRIPT_DIR%build-windows.bat" --help
exit /b %ERRORLEVEL%
