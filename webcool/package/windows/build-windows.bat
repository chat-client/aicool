@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..\..") do set "REPO_ROOT=%%~fI"

if not exist "%REPO_ROOT%\tools\codeformer\setup_codeformer_runtime.ps1" (
    echo [webcool-package] ERROR: CodeFormer setup script was not found.
    echo [webcool-package] Expected: %REPO_ROOT%\tools\codeformer\setup_codeformer_runtime.ps1
    exit /b 1
)
if not exist "%REPO_ROOT%\tools\codeformer\download_codeformer_models.py" (
    echo [webcool-package] ERROR: CodeFormer model downloader was not found.
    exit /b 1
)
if not exist "%SCRIPT_DIR%build-windows.ps1" (
    echo [webcool-package] ERROR: Windows package script was not found.
    exit /b 1
)

set "PYTHON_EXE="
if defined WEBCOOL_PYTHON set "PYTHON_EXE=%WEBCOOL_PYTHON%"

if not defined PYTHON_EXE (
    for /f "usebackq delims=" %%I in (`python -c "import sys; print(sys.executable)" 2^>nul`) do set "PYTHON_EXE=%%I"
)
if not defined PYTHON_EXE (
    for /f "usebackq delims=" %%I in (`py -3 -c "import sys; print(sys.executable)" 2^>nul`) do set "PYTHON_EXE=%%I"
)
if not defined PYTHON_EXE (
    echo [webcool-package] ERROR: Python 3 was not found.
    echo [webcool-package] Install Python 3 or set WEBCOOL_PYTHON to python.exe.
    exit /b 1
)
if not exist "%PYTHON_EXE%" (
    echo [webcool-package] ERROR: Python executable does not exist: %PYTHON_EXE%
    exit /b 1
)

pushd "%REPO_ROOT%" >nul
if errorlevel 1 (
    echo [webcool-package] ERROR: Cannot enter repository root: %REPO_ROOT%
    exit /b 1
)

echo [webcool-package] Repository: %REPO_ROOT%
echo [webcool-package] Python: %PYTHON_EXE%
echo [webcool-package] [1/3] Preparing the Windows CodeFormer runtime...
powershell -NoProfile -ExecutionPolicy Bypass ^
    -File "%REPO_ROOT%\tools\codeformer\setup_codeformer_runtime.ps1" ^
    -Python "%PYTHON_EXE%"
if errorlevel 1 goto :setup_failed

echo [webcool-package] [2/3] Downloading and verifying CodeFormer models...
"%PYTHON_EXE%" "%REPO_ROOT%\tools\codeformer\download_codeformer_models.py"
if errorlevel 1 goto :models_failed

echo [webcool-package] [3/3] Building Windows packages...
powershell -NoProfile -ExecutionPolicy Bypass ^
    -File "%SCRIPT_DIR%build-windows.ps1" %*
if errorlevel 1 goto :package_failed

echo [webcool-package] Windows package workflow completed successfully.
popd
exit /b 0

:setup_failed
set "EXIT_CODE=%ERRORLEVEL%"
echo [webcool-package] ERROR: CodeFormer runtime setup failed with exit code %EXIT_CODE%.
goto :failed

:models_failed
set "EXIT_CODE=%ERRORLEVEL%"
echo [webcool-package] ERROR: CodeFormer model download failed with exit code %EXIT_CODE%.
goto :failed

:package_failed
set "EXIT_CODE=%ERRORLEVEL%"
echo [webcool-package] ERROR: Windows package build failed with exit code %EXIT_CODE%.

:failed
popd
exit /b %EXIT_CODE%
