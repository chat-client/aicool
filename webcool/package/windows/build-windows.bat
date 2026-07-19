@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..\..") do set "REPO_ROOT=%%~fI"
set "MAIN_ONLY="

if /i "%~1"=="--help" goto :usage
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage

for %%A in (%*) do (
    if /i "%%~A"=="-MainOnly" set "MAIN_ONLY=1"
    if /i "%%~A"=="/MainOnly" set "MAIN_ONLY=1"
)

if not exist "%REPO_ROOT%\tools\codeformer\setup_codeformer_runtime.ps1" (
    echo [webcool-package] ERROR: CodeFormer setup script was not found.
    echo [webcool-package] Expected: %REPO_ROOT%\tools\codeformer\setup_codeformer_runtime.ps1
    set "EXIT_CODE=1"
    goto :failed_no_pop
)
if not exist "%REPO_ROOT%\tools\codeformer\setup_codeformer_runtime.bat" (
    echo [webcool-package] ERROR: CodeFormer setup batch script was not found.
    echo [webcool-package] Expected: %REPO_ROOT%\tools\codeformer\setup_codeformer_runtime.bat
    set "EXIT_CODE=1"
    goto :failed_no_pop
)
if not exist "%REPO_ROOT%\tools\codeformer\download_codeformer_models.py" (
    echo [webcool-package] ERROR: CodeFormer model downloader was not found.
    set "EXIT_CODE=1"
    goto :failed_no_pop
)
if not exist "%SCRIPT_DIR%build-windows.ps1" (
    echo [webcool-package] ERROR: Windows package script was not found.
    set "EXIT_CODE=1"
    goto :failed_no_pop
)

set "PYTHON_EXE="
set "FOUND_PYTHON="
set "CODEFORMER_VENV_PYTHON=%REPO_ROOT%\tools\codeformer\venv\windows\Scripts\python.exe"
set "CODEFORMER_LOCAL_PYTHON=%REPO_ROOT%\tools\codeformer\python\windows\python.exe"

if not defined MAIN_ONLY (
    if defined WEBCOOL_PYTHON (
        if exist "%WEBCOOL_PYTHON%" (
            set "PYTHON_EXE=%WEBCOOL_PYTHON%"
        ) else (
            call :detect_python "%WEBCOOL_PYTHON%" ""
            if defined FOUND_PYTHON set "PYTHON_EXE=!FOUND_PYTHON!"
        )
    )
    if not defined PYTHON_EXE if exist "%CODEFORMER_LOCAL_PYTHON%" (
        set "PYTHON_EXE=%CODEFORMER_LOCAL_PYTHON%"
    )
    if not defined PYTHON_EXE (
        call :detect_python "python" ""
        if defined FOUND_PYTHON set "PYTHON_EXE=!FOUND_PYTHON!"
    )
    if not defined PYTHON_EXE (
        call :detect_python "py" "-3"
        if defined FOUND_PYTHON set "PYTHON_EXE=!FOUND_PYTHON!"
    )
    if not defined PYTHON_EXE (
        call :detect_python "python3" ""
        if defined FOUND_PYTHON set "PYTHON_EXE=!FOUND_PYTHON!"
    )
    if not defined PYTHON_EXE if exist "%CODEFORMER_VENV_PYTHON%" (
        set "PYTHON_EXE=%CODEFORMER_VENV_PYTHON%"
    )
    if not defined PYTHON_EXE (
        echo [webcool-package] Python 3 was not found; preparing local CodeFormer Python runtime...
        call "%REPO_ROOT%\tools\codeformer\setup_codeformer_runtime.bat"
        if errorlevel 1 goto :setup_failed
        if exist "%CODEFORMER_VENV_PYTHON%" (
            set "PYTHON_EXE=%CODEFORMER_VENV_PYTHON%"
        ) else if exist "%CODEFORMER_LOCAL_PYTHON%" (
            set "PYTHON_EXE=%CODEFORMER_LOCAL_PYTHON%"
        )
    )
    if not defined PYTHON_EXE (
        echo [webcool-package] ERROR: Python 3 was not found after local setup.
        echo [webcool-package] Use -MainOnly to build only the main Windows package without AI assets.
        set "EXIT_CODE=1"
        goto :failed_no_pop
    )
    if not exist "!PYTHON_EXE!" (
        echo [webcool-package] ERROR: Python executable does not exist: !PYTHON_EXE!
        set "EXIT_CODE=1"
        goto :failed_no_pop
    )
)

pushd "%REPO_ROOT%" >nul
if errorlevel 1 (
    echo [webcool-package] ERROR: Cannot enter repository root: %REPO_ROOT%
    set "EXIT_CODE=1"
    goto :failed_no_pop
)

echo [webcool-package] Repository: %REPO_ROOT%
if defined MAIN_ONLY (
    echo [webcool-package] Main-only package requested; skipping CodeFormer runtime preparation.
) else (
    echo [webcool-package] Python: !PYTHON_EXE!
    echo [webcool-package] [1/3] Preparing the Windows CodeFormer runtime...
    call "%REPO_ROOT%\tools\codeformer\setup_codeformer_runtime.bat" "!PYTHON_EXE!"
    if errorlevel 1 goto :setup_failed
    if exist "%CODEFORMER_VENV_PYTHON%" set "PYTHON_EXE=%CODEFORMER_VENV_PYTHON%"

    echo [webcool-package] [2/3] Downloading and verifying CodeFormer models...
    "!PYTHON_EXE!" "%REPO_ROOT%\tools\codeformer\download_codeformer_models.py"
    if errorlevel 1 goto :models_failed
)

echo [webcool-package] [3/3] Building Windows packages...
powershell -NoProfile -ExecutionPolicy Bypass ^
    -File "%SCRIPT_DIR%build-windows.ps1" %*
if errorlevel 1 goto :package_failed

echo [webcool-package] Windows package workflow completed successfully.
popd
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

:usage
echo webcool Windows package workflow
echo.
echo Usage:
echo   build-windows.bat [options]
echo   build-debug.bat [options]
echo   build-release.bat [options]
echo.
echo Module selection:
echo   -MainOnly       Build/package only the main webcool module; skip AI assets.
echo   -AiOnly         Build/package only the AI module; skip the main package.
echo.
echo Build options:
echo   -Configuration Debug^|Release
echo   -Platform x64
echo   -SkipBuild      Skip MSBuild and package existing binaries.
echo   -NoZip          Do not create zip archives.
echo   -NoInstaller    Do not create Inno Setup installers.
echo   -OutputDir DIR
echo   -FfmpegPath PATH
echo   -MsBuildPath PATH
echo   -InnoSetupPath PATH
echo   -Version VERSION
echo.
echo Examples:
echo   build-release.bat -MainOnly
echo   build-release.bat -AiOnly -SkipBuild
echo   build-debug.bat -MainOnly -NoInstaller
echo   build-windows.bat -Configuration Release -NoZip
exit /b 0

:failed_no_pop
exit /b %EXIT_CODE%

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
