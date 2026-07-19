@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "SETUP_SCRIPT=%SCRIPT_DIR%setup_codeformer_runtime.ps1"
set "LOCAL_PYTHON_DIR=%SCRIPT_DIR%python\windows"
set "LOCAL_PYTHON_EXE=%LOCAL_PYTHON_DIR%\python.exe"
set "DOWNLOAD_DIR=%SCRIPT_DIR%downloads"
set "PYTHON_VERSION=3.10.11"
set "PYTHON_EXE="
set "FOUND_PYTHON="

if defined CODEFORMER_PYTHON_VERSION set "PYTHON_VERSION=%CODEFORMER_PYTHON_VERSION%"
set "PYTHON_URL=https://www.python.org/ftp/python/%PYTHON_VERSION%/python-%PYTHON_VERSION%-amd64.exe"
if defined CODEFORMER_PYTHON_URL set "PYTHON_URL=%CODEFORMER_PYTHON_URL%"
set "PYTHON_INSTALLER=%DOWNLOAD_DIR%\python-%PYTHON_VERSION%-amd64.exe"

if not exist "%SETUP_SCRIPT%" (
    echo [codeformer-runtime] ERROR: setup_codeformer_runtime.ps1 was not found.
    echo [codeformer-runtime] Expected: %SETUP_SCRIPT%
    exit /b 1
)

if not "%~1"=="" (
    if exist "%~1" (
        set "PYTHON_EXE=%~1"
    ) else (
        call :detect_python "%~1" ""
        if defined FOUND_PYTHON set "PYTHON_EXE=%FOUND_PYTHON%"
    )
)
if not defined PYTHON_EXE if defined WEBCOOL_PYTHON (
    if exist "%WEBCOOL_PYTHON%" (
        set "PYTHON_EXE=%WEBCOOL_PYTHON%"
    ) else (
        call :detect_python "%WEBCOOL_PYTHON%" ""
        if defined FOUND_PYTHON set "PYTHON_EXE=%FOUND_PYTHON%"
    )
)
if not defined PYTHON_EXE (
    call :detect_python "python" ""
    if defined FOUND_PYTHON set "PYTHON_EXE=%FOUND_PYTHON%"
)
if not defined PYTHON_EXE (
    call :detect_python "py" "-3"
    if defined FOUND_PYTHON set "PYTHON_EXE=%FOUND_PYTHON%"
)
if not defined PYTHON_EXE (
    call :detect_python "python3" ""
    if defined FOUND_PYTHON set "PYTHON_EXE=%FOUND_PYTHON%"
)
if not defined PYTHON_EXE if exist "%LOCAL_PYTHON_EXE%" set "PYTHON_EXE=%LOCAL_PYTHON_EXE%"
if not defined PYTHON_EXE (
    call :install_local_python
    if errorlevel 1 exit /b 1
    if exist "%LOCAL_PYTHON_EXE%" set "PYTHON_EXE=%LOCAL_PYTHON_EXE%"
)

if not defined PYTHON_EXE (
    echo [codeformer-runtime] ERROR: Python 3 was not found.
    echo [codeformer-runtime] Install Python 3, set WEBCOOL_PYTHON to a real python.exe path, or pass python.exe as the first argument.
    echo [codeformer-runtime] Windows Store app aliases under Microsoft\WindowsApps are ignored.
    exit /b 1
)

echo [codeformer-runtime] Directory: %SCRIPT_DIR%
echo [codeformer-runtime] Python: %PYTHON_EXE%
echo [codeformer-runtime] Installing runtime into: %SCRIPT_DIR%venv\windows

powershell -NoProfile -ExecutionPolicy Bypass ^
    -File "%SETUP_SCRIPT%" ^
    -Python "%PYTHON_EXE%"
if errorlevel 1 goto :setup_failed

echo [codeformer-runtime] CodeFormer Windows runtime completed successfully.
exit /b 0

:install_local_python
echo [codeformer-runtime] Python 3 was not found; downloading a local Python runtime...
echo [codeformer-runtime] URL: %PYTHON_URL%
echo [codeformer-runtime] Target: %LOCAL_PYTHON_DIR%

if not exist "%DOWNLOAD_DIR%" mkdir "%DOWNLOAD_DIR%"
if errorlevel 1 exit /b 1

if not exist "%PYTHON_INSTALLER%" (
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$ErrorActionPreference='Stop'; [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%PYTHON_URL%' -OutFile '%PYTHON_INSTALLER%'"
    if errorlevel 1 (
        echo [codeformer-runtime] ERROR: Failed to download Python installer.
        exit /b 1
    )
)

if not exist "%LOCAL_PYTHON_DIR%" mkdir "%LOCAL_PYTHON_DIR%"
if errorlevel 1 exit /b 1

start /wait "" "%PYTHON_INSTALLER%" /quiet InstallAllUsers=0 TargetDir="%LOCAL_PYTHON_DIR%" Include_pip=1 Include_launcher=0 PrependPath=0 Include_test=0 Shortcuts=0
if errorlevel 1 (
    echo [codeformer-runtime] ERROR: Python installer failed.
    exit /b 1
)
if not exist "%LOCAL_PYTHON_EXE%" (
    echo [codeformer-runtime] ERROR: Local Python was not installed: %LOCAL_PYTHON_EXE%
    exit /b 1
)
exit /b 0

:detect_python
set "FOUND_PYTHON="
set "PYTHON_CMD=%~1"
set "PYTHON_ARGS=%~2"
for /f "usebackq delims=" %%I in (`"%PYTHON_CMD%" %PYTHON_ARGS% -c "import os, sys; print(os.path.realpath(sys.executable))" 2^>nul`) do set "FOUND_PYTHON=%%I"
if not defined FOUND_PYTHON exit /b 1
echo %FOUND_PYTHON% | findstr /i /c:"\Microsoft\WindowsApps\" >nul
if not errorlevel 1 (
    set "FOUND_PYTHON="
    exit /b 1
)
exit /b 0

:setup_failed
set "EXIT_CODE=%ERRORLEVEL%"
echo [codeformer-runtime] ERROR: CodeFormer runtime setup failed with exit code %EXIT_CODE%.
exit /b %EXIT_CODE%
