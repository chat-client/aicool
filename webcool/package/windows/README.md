# webcool Windows packages

Build separate Windows x64 main and optional AI packages. When Inno Setup is
available, each package also gets its own guided installer EXE:

```powershell
powershell -ExecutionPolicy Bypass -File .\webcool\package\windows\build-windows.ps1
```

To prepare the Windows CodeFormer venv, download/verify its models, and then
build the packages in one command, use the complete batch wrapper from the
repository root:

```bat
webcool\package\windows\build-windows.bat
```

It locates Python 3 automatically and stops immediately if any of the three
stages fails. Set `WEBCOOL_PYTHON` to an absolute `python.exe` path when a
specific interpreter is required:

```bat
set "WEBCOOL_PYTHON=C:\Python312\python.exe"
webcool\package\windows\build-windows.bat -Configuration Release
```

All extra arguments are passed to `build-windows.ps1`, for example:

```bat
webcool\package\windows\build-windows.bat -Configuration Release -NoInstaller
webcool\package\windows\build-windows.bat -AiOnly -SkipBuild
```

Or use the batch wrappers from the repository root:

```bat
webcool\package\windows\build-debug.bat
webcool\package\windows\build-release.bat
```

Common options:

```powershell
powershell -ExecutionPolicy Bypass -File .\webcool\package\windows\build-windows.ps1 -Configuration Release
powershell -ExecutionPolicy Bypass -File .\webcool\package\windows\build-windows.ps1 -SkipBuild
powershell -ExecutionPolicy Bypass -File .\webcool\package\windows\build-windows.ps1 -FfmpegPath D:\tools\ffmpeg.exe
powershell -ExecutionPolicy Bypass -File .\webcool\package\windows\build-windows.ps1 -InnoSetupPath "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
powershell -ExecutionPolicy Bypass -File .\webcool\package\windows\build-windows.ps1 -MainOnly
powershell -ExecutionPolicy Bypass -File .\webcool\package\windows\build-windows.ps1 -AiOnly -SkipBuild
```

The batch wrappers pass through extra options:

```bat
webcool\package\windows\build-debug.bat -SkipBuild
webcool\package\windows\build-release.bat -FfmpegPath D:\tools\ffmpeg.exe
```

## Installer EXE

Install Inno Setup 6 before running the package script:

https://jrsoftware.org/isinfo.php

If `ISCC.exe` is available on `PATH` or in the default Inno Setup install
location, a release build creates:

- `webcool/package/windows/out/webcool-<version>-windows-x64-release-setup.exe`
- `webcool/package/windows/out/webcool-<version>-windows-x64-release.iss`
- `webcool/package/windows/out/webcool-ai-models-<version>-windows-x64-release-setup.exe`
- `webcool/package/windows/out/webcool-ai-models-<version>-windows-x64-release.iss`

The setup EXE shows a normal installation wizard. It defaults to:

- `C:\Program Files\webcool` on 64-bit Windows

Users can change the install directory in the wizard. The installer creates
Start Menu shortcuts and an optional desktop shortcut. Runtime uploads are
stored under:

- `C:\ProgramData\webcool\uploads`

This avoids writing user data into `Program Files`.

Useful installer options:

```powershell
powershell -ExecutionPolicy Bypass -File .\webcool\package\windows\build-windows.ps1 -NoInstaller
powershell -ExecutionPolicy Bypass -File .\webcool\package\windows\build-windows.ps1 -NoZip
```

## Authenticode signing

Release builds can sign the staged `webcool.exe` before it is archived and
sign each generated Setup EXE. Signing uses SHA-256, an RFC 3161 timestamp,
and verifies every signature with the Windows Authenticode policy before the
build succeeds.

For a code-signing certificate installed in the current user's Windows
certificate store:

```powershell
Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
    Format-Table Subject, Thumbprint, NotAfter
```

```bat
webcool\package\windows\build-release.bat -MainOnly ^
  -SignCertificateThumbprint "CERTIFICATE_SHA1_THUMBPRINT" ^
  -RequireSigning
```

Add `-SignCertificateMachineStore` when the certificate is in the local
machine store. To use a PFX/P12 file, keep its password out of the command
line and repository:

```bat
set "WEBCOOL_SIGN_CERT_PASSWORD=your-pfx-password"
webcool\package\windows\build-release.bat -MainOnly ^
  -SignCertificatePath "D:\secrets\webcool-code-signing.pfx" ^
  -RequireSigning
```

The password variable name can be changed with
`-SignCertificatePasswordEnv`. `signtool.exe` is discovered from `PATH` or
the Windows 10/11 SDK; otherwise pass `-SignToolPath`. The default timestamp
service is `http://timestamp.digicert.com` and can be changed with
`-SignTimestampUrl`.

The PFX/P12 file and password must remain outside source control. Use a
publicly trusted OV or EV code-signing certificate for distributed releases;
a self-signed certificate is useful for testing but does not establish
SmartScreen reputation.

Main package output:

- `webcool/package/windows/out/stage/webcool-<version>-windows-x64-debug/`
- `webcool/package/windows/out/webcool-<version>-windows-x64-debug.zip`
- `webcool/package/windows/out/webcool-<version>-windows-x64-debug-setup.exe`
- `webcool/package/windows/out/stage/webcool-<version>-windows-x64-release/`
- `webcool/package/windows/out/webcool-<version>-windows-x64-release.zip`
- `webcool/package/windows/out/webcool-<version>-windows-x64-release-setup.exe`

AI package output follows the same naming scheme:

- `webcool/package/windows/out/stage/webcool-ai-models-<version>-windows-x64-release/`
- `webcool/package/windows/out/webcool-ai-models-<version>-windows-x64-release.zip`
- `webcool/package/windows/out/webcool-ai-models-<version>-windows-x64-release-setup.exe`

The staged package contains:

- `webcool.exe`
- `sqlite.dll`
- `zlib.dll`
- `ffmpeg.exe`
- `html/`
- `run-webcool.ps1`
- `install.ps1`
- `uninstall.ps1`

Run after extracting:

```powershell
powershell -ExecutionPolicy Bypass -File .\run-webcool.ps1
```

Install to the current user profile:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

The main package intentionally contains no AI runtime or model files. Before
building the Windows AI package, prepare the Windows-only CodeFormer venv:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\codeformer\setup_codeformer_runtime.ps1
python .\tools\codeformer\download_codeformer_models.py
```

The AI package contains the Windows Real-ESRGAN runtime, supporting DLLs,
shared models, and a self-contained Windows CodeFormer Python runtime. Install
its Setup EXE after the main Setup EXE, or extract its ZIP and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\install-ai.ps1
```

Core ML and Restormer assets remain macOS-only because those runtimes use Apple
Core ML. Windows CodeFormer uses `tools\codeformer\venv\windows`; it never
packages the macOS or Ubuntu venv.
