@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "DOWNLOADER=%SCRIPT_DIR%download_codeformer_models.py"
set "VENV_PYTHON=%SCRIPT_DIR%venv\windows\Scripts\python.exe"
set "LOCAL_PYTHON=%SCRIPT_DIR%python\windows\python.exe"
set "PYTHON_EXE="
set "FOUND_PYTHON="

if not exist "%DOWNLOADER%" (
    echo [codeformer-models] ERROR: download_codeformer_models.py was not found.
    echo [codeformer-models] Expected: %DOWNLOADER%
    exit /b 1
)

if exist "%VENV_PYTHON%" set "PYTHON_EXE=%VENV_PYTHON%"
if not defined PYTHON_EXE if exist "%LOCAL_PYTHON%" set "PYTHON_EXE=%LOCAL_PYTHON%"
if not defined PYTHON_EXE if defined WEBCOOL_PYTHON if exist "%WEBCOOL_PYTHON%" set "PYTHON_EXE=%WEBCOOL_PYTHON%"
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

if not defined PYTHON_EXE (
    echo [codeformer-models] ERROR: Python 3 was not found.
    echo [codeformer-models] Run setup_codeformer_runtime.bat first, or set WEBCOOL_PYTHON to a real python.exe path.
    echo [codeformer-models] Windows Store app aliases under Microsoft\WindowsApps are ignored.
    exit /b 1
)

echo [codeformer-models] Python: %PYTHON_EXE%
"%PYTHON_EXE%" "%DOWNLOADER%" %*
if errorlevel 1 goto :download_failed

echo [codeformer-models] CodeFormer models completed successfully.
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

:download_failed
set "EXIT_CODE=%ERRORLEVEL%"
echo [codeformer-models] ERROR: CodeFormer model download failed with exit code %EXIT_CODE%.
exit /b %EXIT_CODE%
