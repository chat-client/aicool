# webcool Windows package

Build a self-contained Windows x64 package and, when Inno Setup is available,
a guided Windows installer EXE:

```powershell
powershell -ExecutionPolicy Bypass -File .\webcool\package\windows\build-windows.ps1
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
location, the script creates:

- `webcool/package/windows/out/webcool-<version>-windows-x64-release-setup.exe`
- `webcool/package/windows/out/webcool-<version>-windows-x64-release.iss`

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

Output:

- `webcool/package/windows/out/stage/webcool-<version>-windows-x64-debug/`
- `webcool/package/windows/out/webcool-<version>-windows-x64-debug.zip`
- `webcool/package/windows/out/webcool-<version>-windows-x64-debug-setup.exe`
- `webcool/package/windows/out/stage/webcool-<version>-windows-x64-release/`
- `webcool/package/windows/out/webcool-<version>-windows-x64-release.zip`
- `webcool/package/windows/out/webcool-<version>-windows-x64-release-setup.exe`

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
