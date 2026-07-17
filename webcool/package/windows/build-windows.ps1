param(
    [string]$Version = "",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [switch]$SkipBuild,
    [switch]$NoZip,
    [switch]$NoInstaller,
    [switch]$MainOnly,
    [switch]$AiOnly,
    [string]$OutputDir = "",
    [string]$FfmpegPath = "",
    [string]$MsBuildPath = "",
    [string]$InnoSetupPath = ""
)

$ErrorActionPreference = "Stop"

function Log([string]$Message) {
    Write-Host "[webcool-package] $Message"
}

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir "..\..\..")).Path
}

function Find-MSBuild {
    param([string]$ExplicitPath)

    if ($ExplicitPath -and (Test-Path -LiteralPath $ExplicitPath)) {
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\Current\Bin\amd64\MSBuild.exe" 2>$null | Select-Object -First 1
        if ($found -and (Test-Path -LiteralPath $found)) {
            return $found
        }
    }

    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "MSBuild.exe not found. Pass -MsBuildPath or install Visual Studio Build Tools."
}

function Find-InnoSetup {
    param([string]$ExplicitPath)

    if ($ExplicitPath -and (Test-Path -LiteralPath $ExplicitPath)) {
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $cmd = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
    if ($cmd -and (Test-Path -LiteralPath $cmd.Source)) {
        return $cmd.Source
    }

    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 5\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 5\ISCC.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    return ""
}

function Copy-DirectoryContent {
    param([string]$Source, [string]$Destination)
    if (!(Test-Path -LiteralPath $Source)) {
        throw "Directory not found: $Source"
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item -Path (Join-Path $Source "*") -Destination $Destination -Recurse -Force
}

function Escape-InnoString {
    param([string]$Value)
    return ($Value -replace '"', '""')
}

function Write-TextFileUtf8Bom {
    param([string]$Path, [string]$Value)
    $encoding = New-Object System.Text.UTF8Encoding $true
    [System.IO.File]::WriteAllText($Path, $Value, $encoding)
}

function Write-InnoSetupScript {
    param(
        [string]$PackageRoot,
        [string]$OutputDir,
        [string]$PackageName,
        [string]$Version,
        [string]$Platform,
        [string]$Configuration
    )

    $issPath = Join-Path $OutputDir "$PackageName.iss"
    $setupBaseName = "$PackageName-setup"
    $source = Escape-InnoString $PackageRoot
    $out = Escape-InnoString $OutputDir
    $appVersion = Escape-InnoString $Version
    $comments = Escape-InnoString "Platform=$Platform; Configuration=$Configuration"

    $iss = @"
#define MyAppName "webcool"
#define MyAppVersion "$appVersion"
#define MyAppPublisher "Aicool"
#define MyAppExeName "webcool.exe"

[Setup]
AppId={{4B3CB30F-3F5C-4BC9-98C5-4478D6C69080}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppComments=$comments
DefaultDirName={autopf}\webcool
DefaultGroupName=webcool
DisableProgramGroupPage=yes
AllowNoIcons=yes
OutputDir=$out
OutputBaseFilename=$setupBaseName
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=$source\webcool.ico
UninstallDisplayIcon={app}\webcool.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Dirs]
Name: "{app}"; Permissions: users-modify
Name: "{commonappdata}\webcool\uploads"; Permissions: users-modify

[Files]
Source: "$source\webcool.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "$source\webcool.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "$source\sqlite.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "$source\zlib.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "$source\ffmpeg.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "$source\run-webcool.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "$source\install.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "$source\uninstall.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "$source\README-Windows.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "$source\html\*"; DestDir: "{app}\html"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\webcool Control Panel"; Filename: "{app}\webcool.exe"; Parameters: "-G -d ""{commonappdata}\webcool\uploads"" -w ""{app}\html"" -S ""{app}\sqlite.dll"" -F ""{app}\ffmpeg.exe"""; WorkingDir: "{app}"
Name: "{group}\Open webcool"; Filename: "http://127.0.0.1:8080/"
Name: "{group}\Uninstall webcool"; Filename: "{uninstallexe}"
Name: "{autodesktop}\webcool Control Panel"; Filename: "{app}\webcool.exe"; Parameters: "-G -d ""{commonappdata}\webcool\uploads"" -w ""{app}\html"" -S ""{app}\sqlite.dll"" -F ""{app}\ffmpeg.exe"""; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\webcool.exe"; Parameters: "-G -d ""{commonappdata}\webcool\uploads"" -w ""{app}\html"" -S ""{app}\sqlite.dll"" -F ""{app}\ffmpeg.exe"""; Description: "Start webcool Control Panel"; Flags: nowait postinstall skipifsilent
"@

    Write-TextFileUtf8Bom -Path $issPath -Value $iss
    return $issPath
}

function Write-AiInnoSetupScript {
    param(
        [string]$PackageRoot,
        [string]$OutputDir,
        [string]$PackageName,
        [string]$Version,
        [string]$Platform,
        [string]$Configuration
    )

    $issPath = Join-Path $OutputDir "$PackageName.iss"
    $setupBaseName = "$PackageName-setup"
    $source = Escape-InnoString $PackageRoot
    $out = Escape-InnoString $OutputDir
    $appVersion = Escape-InnoString $Version
    $comments = Escape-InnoString "Optional AI assets; Platform=$Platform; Configuration=$Configuration"

    $iss = @"
#define MyAppName "webcool AI Models"
#define MyAppVersion "$appVersion"
#define MyAppPublisher "Aicool"

[Setup]
AppId={{D50F87BC-7F51-49ED-AAC8-E57D798D54A0}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppComments=$comments
DefaultDirName={autopf}\webcool
DisableProgramGroupPage=yes
AllowNoIcons=yes
OutputDir=$out
OutputBaseFilename=$setupBaseName
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "$source\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
"@

    Write-TextFileUtf8Bom -Path $issPath -Value $iss
    return $issPath
}

function Write-AiPackageScripts {
    param([string]$PackageRoot)

    $installScript = @'
param([string]$InstallDir = "$env:LOCALAPPDATA\Programs\webcool")

$ErrorActionPreference = "Stop"
$SourceDir = Split-Path -Parent $PSCommandPath
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
$items = @("realesrgan-ncnn-vulkan.exe", "vcomp140.dll", "vcomp140d.dll", "models", "codeformer", "libexec", "REALESRGAN-LICENSE", "CODEFORMER.md", "CODEFORMER-CONSTRAINTS.txt", "AI-PACKAGE-VERSION")
foreach ($item in $items) {
    $src = Join-Path $SourceDir $item
    if (Test-Path -LiteralPath $src) {
        Copy-Item -LiteralPath $src -Destination $InstallDir -Recurse -Force
    }
}
Write-Host "Installed webcool AI assets to: $InstallDir"
'@

    $readme = @'
# webcool AI Models for Windows

Install the main webcool package first. For the portable ZIP installation run:

```powershell
powershell -ExecutionPolicy Bypass -File .\install-ai.ps1
```

The Windows AI package contains the Real-ESRGAN runtime and models plus a
self-contained CodeFormer Python runtime. Build it on Windows after running
`tools\codeformer\setup_codeformer_runtime.ps1`.
'@

    Set-Content -LiteralPath (Join-Path $PackageRoot "install-ai.ps1") -Value $installScript -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $PackageRoot "README-AI-Windows.md") -Value $readme -Encoding UTF8
}

function Write-PackageScripts {
    param([string]$PackageRoot)

    $runScript = @'
param(
    [string]$Listen = "0.0.0.0:8080",
    [string]$UploadDir = "",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArgs
)

$ErrorActionPreference = "Stop"
$BaseDir = Split-Path -Parent $PSCommandPath
if (!$UploadDir) {
    $UploadDir = Join-Path $BaseDir "uploads"
}

New-Item -ItemType Directory -Force -Path $UploadDir | Out-Null
$env:AICOOL_SQLITE_LIB = Join-Path $BaseDir "sqlite.dll"
$env:AICOOL_FFMPEG = Join-Path $BaseDir "ffmpeg.exe"

$argsList = @(
    "-s", $Listen,
    "-d", $UploadDir,
    "-w", (Join-Path $BaseDir "html"),
    "-S", $env:AICOOL_SQLITE_LIB,
    "-F", $env:AICOOL_FFMPEG
)
if ($ExtraArgs) {
    $argsList += $ExtraArgs
}

& (Join-Path $BaseDir "webcool.exe") @argsList
'@

    $installScript = @'
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\webcool",
    [switch]$DesktopShortcut
)

$ErrorActionPreference = "Stop"
$SourceDir = Split-Path -Parent $PSCommandPath
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

$items = @("webcool.exe", "sqlite.dll", "zlib.dll", "ffmpeg.exe", "html", "run-webcool.ps1", "uninstall.ps1")
foreach ($item in $items) {
    $src = Join-Path $SourceDir $item
    if (Test-Path -LiteralPath $src) {
        Copy-Item -LiteralPath $src -Destination $InstallDir -Recurse -Force
    }
}
New-Item -ItemType Directory -Force -Path (Join-Path $InstallDir "uploads") | Out-Null

if ($DesktopShortcut) {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath("Desktop")) "webcool.lnk"))
    $shortcut.TargetPath = "powershell.exe"
    $shortcut.Arguments = "-ExecutionPolicy Bypass -File `"$InstallDir\run-webcool.ps1`""
    $shortcut.WorkingDirectory = $InstallDir
    $shortcut.Save()
}

Write-Host "Installed webcool to: $InstallDir"
Write-Host "Start with:"
Write-Host "  powershell -ExecutionPolicy Bypass -File `"$InstallDir\run-webcool.ps1`""
'@

    $uninstallScript = @'
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\Programs\webcool",
    [switch]$RemoveUploads
)

$ErrorActionPreference = "Stop"
if (!(Test-Path -LiteralPath $InstallDir)) {
    Write-Host "Install directory not found: $InstallDir"
    exit 0
}

if ($RemoveUploads) {
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
} else {
    Get-ChildItem -LiteralPath $InstallDir -Force |
        Where-Object { $_.Name -ne "uploads" } |
        Remove-Item -Recurse -Force
}

$shortcut = Join-Path ([Environment]::GetFolderPath("Desktop")) "webcool.lnk"
if (Test-Path -LiteralPath $shortcut) {
    Remove-Item -LiteralPath $shortcut -Force
}
Write-Host "Uninstalled webcool from: $InstallDir"
'@

    $readme = @'
# webcool for Windows

## Direct run

```powershell
powershell -ExecutionPolicy Bypass -File .\run-webcool.ps1
```

Default URL: http://127.0.0.1:8080/

## Install to user profile

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

Create a desktop shortcut:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1 -DesktopShortcut
```

## Custom listen address or upload directory

```powershell
powershell -ExecutionPolicy Bypass -File .\run-webcool.ps1 -Listen 127.0.0.1:8080 -UploadDir D:\webcool-data
```
'@

    Set-Content -LiteralPath (Join-Path $PackageRoot "run-webcool.ps1") -Value $runScript -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $PackageRoot "install.ps1") -Value $installScript -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $PackageRoot "uninstall.ps1") -Value $uninstallScript -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $PackageRoot "README-Windows.md") -Value $readme -Encoding UTF8
}

function Resolve-ZlibDll {
    param(
        [string]$BinDir,
        [string]$WebcoolRoot,
        [string]$RepoRoot,
        [string]$Platform,
        [string]$Configuration
    )

    $primary = Join-Path $BinDir "zlib.dll"
    if (Test-Path -LiteralPath $primary) {
        return $primary
    }

    $zlibProjectDir = Join-Path $RepoRoot "third-party\zlib-1.2.11\visualc\vc2022"
    $fallbacks = @(
        (Join-Path $WebcoolRoot "$Platform\$Configuration\zlib.dll"),
        (Join-Path $WebcoolRoot "$Configuration\zlib.dll"),
        (Join-Path $zlibProjectDir "$Platform\$Configuration\zlib.dll"),
        (Join-Path $RepoRoot "tools\windows\zlib.dll")
    )

    $found = $fallbacks | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($found) {
        return $found
    }

    throw "zlib.dll not found. Expected it in $BinDir, build zlib from third-party/zlib-1.2.11/visualc/vc2022/zlib.vcxproj, or copy zlib.dll into tools/windows/."
}

$repoRoot = Resolve-RepoRoot
$webcoolRoot = Join-Path $repoRoot "webcool"
$packageRoot = Join-Path $webcoolRoot "package"
$projectFile = Join-Path $webcoolRoot "webcool.vcxproj"
$zlibProject = Join-Path $repoRoot "third-party\zlib-1.2.11\visualc\vc2022\zlib.vcxproj"

if ($MainOnly -and $AiOnly) {
    throw "-MainOnly and -AiOnly cannot be used together."
}
$buildMainPackage = !$AiOnly
$buildAiPackage = !$MainOnly

if (!$OutputDir) {
    $OutputDir = Join-Path $packageRoot "windows\out"
}
$OutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)

if (!$SkipBuild -and $buildMainPackage) {
    $msbuild = Find-MSBuild -ExplicitPath $MsBuildPath
    Log "building webcool ($Configuration|$Platform)"
    & $msbuild $projectFile "/p:Configuration=$Configuration" "/p:Platform=$Platform" /m
    if (!(Test-Path -LiteralPath $zlibProject)) {
        throw "zlib project not found: $zlibProject"
    }
    Log "building zlib ($Configuration|$Platform)"
    & $msbuild $zlibProject "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:SolutionDir=$webcoolRoot\" /m
}

$binDir = Join-Path $webcoolRoot "$Platform\$Configuration"
$webcoolExe = Join-Path $binDir "webcool.exe"
$sqliteDll = Join-Path $binDir "sqlite.dll"
if ($buildMainPackage) {
    if (!(Test-Path -LiteralPath $webcoolExe)) {
        throw "webcool.exe not found: $webcoolExe"
    }
    if (!(Test-Path -LiteralPath $sqliteDll)) {
        $sqliteFallbacks = @(
            (Join-Path $webcoolRoot "$Platform\Debug\sqlite.dll"),
            (Join-Path $repoRoot "tools\windows\sqlite.dll")
        )
        $sqliteDll = $sqliteFallbacks | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
        if (!$sqliteDll) {
            throw "sqlite.dll not found. Expected it in $binDir or copy sqlite.dll into the build output."
        }
        Log "using sqlite.dll: $sqliteDll"
    }

    $zlibDll = Resolve-ZlibDll -BinDir $binDir -WebcoolRoot $webcoolRoot -RepoRoot $repoRoot -Platform $Platform -Configuration $Configuration
    if ($zlibDll -ne (Join-Path $binDir "zlib.dll")) {
        Log "using zlib.dll: $zlibDll"
    }
}

if (!$Version) {
    if (Test-Path -LiteralPath $webcoolExe) {
        $detected = & $webcoolExe -v 2>$null | Select-Object -First 1
        if ($detected) {
            $Version = $detected.Trim()
        }
    }
}
if (!$Version) {
    $Version = "1.0.0"
}

if ($buildMainPackage) {
    if (!$FfmpegPath) {
        $FfmpegPath = Join-Path $repoRoot "tools\windows\ffmpeg.exe"
    }
    $FfmpegPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($FfmpegPath)
    if (!(Test-Path -LiteralPath $FfmpegPath)) {
        throw "ffmpeg.exe not found: $FfmpegPath"
    }
}

$packageName = "webcool-$Version-windows-$Platform-$($Configuration.ToLowerInvariant())"
$aiPackageName = "webcool-ai-models-$Version-windows-$Platform-$($Configuration.ToLowerInvariant())"
$stageRoot = Join-Path $OutputDir "stage"
$appRoot = Join-Path $stageRoot $packageName
$aiRoot = Join-Path $stageRoot $aiPackageName
$zipPath = Join-Path $OutputDir "$packageName.zip"
$aiZipPath = Join-Path $OutputDir "$aiPackageName.zip"
$setupPath = Join-Path $OutputDir "$packageName-setup.exe"
$aiSetupPath = Join-Path $OutputDir "$aiPackageName-setup.exe"

if ($buildMainPackage) {
    Log "staging main package: $appRoot"
    if (Test-Path -LiteralPath $appRoot) {
        Remove-Item -LiteralPath $appRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $appRoot | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $appRoot "uploads") | Out-Null

    Copy-Item -LiteralPath $webcoolExe -Destination (Join-Path $appRoot "webcool.exe") -Force
    Copy-Item -LiteralPath (Join-Path $webcoolRoot "res\webcool.ico") -Destination (Join-Path $appRoot "webcool.ico") -Force
    Copy-Item -LiteralPath $sqliteDll -Destination (Join-Path $appRoot "sqlite.dll") -Force
    Copy-Item -LiteralPath $zlibDll -Destination (Join-Path $appRoot "zlib.dll") -Force
    Copy-Item -LiteralPath $FfmpegPath -Destination (Join-Path $appRoot "ffmpeg.exe") -Force
    Copy-DirectoryContent -Source (Join-Path $webcoolRoot "html") -Destination (Join-Path $appRoot "html")
    Write-PackageScripts -PackageRoot $appRoot
}

if ($buildAiPackage) {
    Log "staging AI package: $aiRoot"
    if (Test-Path -LiteralPath $aiRoot) {
        Remove-Item -LiteralPath $aiRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $aiRoot | Out-Null

    $realEsrganExe = Join-Path $repoRoot "tools\windows\realesrgan-ncnn-vulkan.exe"
    if (!(Test-Path -LiteralPath $realEsrganExe)) {
        throw "Real-ESRGAN runtime not found: $realEsrganExe"
    }
    Copy-Item -LiteralPath $realEsrganExe -Destination (Join-Path $aiRoot "realesrgan-ncnn-vulkan.exe") -Force

    foreach ($dll in @("vcomp140.dll", "vcomp140d.dll")) {
        $dllPath = Join-Path $repoRoot "tools\windows\$dll"
        if (Test-Path -LiteralPath $dllPath) {
            Copy-Item -LiteralPath $dllPath -Destination (Join-Path $aiRoot $dll) -Force
        }
    }
    $modelPath = Join-Path $repoRoot "models\realesrgan\ncnn"
    if (!(Test-Path -LiteralPath $modelPath)) {
        throw "Real-ESRGAN models not found: $modelPath"
    }
    Copy-DirectoryContent -Source $modelPath -Destination (Join-Path $aiRoot "models\realesrgan")

    $codeFormerTools = Join-Path $repoRoot "tools\codeformer"
    $codeFormerSource = Join-Path $codeFormerTools "CodeFormer"
    $codeFormerVenv = Join-Path $codeFormerTools "venv\windows"
    $codeFormerPython = Join-Path $codeFormerVenv "Scripts\python.exe"
    $codeFormerWeights = Join-Path $repoRoot "models\codeformer\weights"
    foreach ($required in @(
        (Join-Path $codeFormerSource "inference_codeformer.py"),
        $codeFormerPython,
        (Join-Path $codeFormerWeights "CodeFormer\codeformer.pth"),
        (Join-Path $codeFormerWeights "CodeFormer\codeformer_inpainting.pth"),
        (Join-Path $codeFormerWeights "facelib\detection_Resnet50_Final.pth"),
        (Join-Path $codeFormerWeights "facelib\parsing_parsenet.pth")
    )) {
        if (!(Test-Path -LiteralPath $required)) {
            throw "Incomplete Windows CodeFormer runtime; missing: $required. Run tools\codeformer\setup_codeformer_runtime.ps1 on Windows first."
        }
    }

    $stagedCodeFormer = Join-Path $aiRoot "codeformer"
    $stagedRepository = Join-Path $stagedCodeFormer "CodeFormer"
    New-Item -ItemType Directory -Force -Path $stagedRepository | Out-Null
    # Do not traverse the development weights junction while copying source.
    # The shared model directory is copied explicitly below.
    Get-ChildItem -LiteralPath $codeFormerSource -Force |
        Where-Object { $_.Name -ne "weights" } |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $stagedRepository -Recurse -Force
        }
    $stagedWeights = Join-Path $stagedRepository "weights"
    Copy-DirectoryContent -Source $codeFormerWeights -Destination $stagedWeights

    # A normal Windows venv points back to the Python installation that created
    # it. Build a relocatable runtime from that base Python and overlay only the
    # packages installed in venv/windows. Without pyvenv.cfg, sys.prefix follows
    # the packaged python.exe location and no target-machine Python is required.
    $pythonBase = (& $codeFormerPython -c "import sys; print(sys.base_prefix)").Trim()
    if (!$pythonBase -or !(Test-Path -LiteralPath (Join-Path $pythonBase "python.exe"))) {
        throw "CodeFormer base Python is not packageable: $pythonBase"
    }
    $portablePython = Join-Path $stagedCodeFormer "venv"
    Copy-DirectoryContent -Source $pythonBase -Destination $portablePython
    $portableSitePackages = Join-Path $portablePython "Lib\site-packages"
    Copy-DirectoryContent -Source (Join-Path $codeFormerVenv "Lib\site-packages") -Destination $portableSitePackages
    $portableConfig = Join-Path $portablePython "pyvenv.cfg"
    if (Test-Path -LiteralPath $portableConfig) {
        Remove-Item -LiteralPath $portableConfig -Force
    }

    $libexec = Join-Path $aiRoot "libexec"
    New-Item -ItemType Directory -Force -Path $libexec | Out-Null
    Copy-Item -LiteralPath (Join-Path $codeFormerTools "codeformer_runner.py") -Destination $libexec -Force
    foreach ($doc in @("CODEFORMER.md", "codeformer-constraints.txt")) {
        $docPath = Join-Path $codeFormerTools $doc
        if (Test-Path -LiteralPath $docPath) {
            $targetName = if ($doc -eq "codeformer-constraints.txt") { "CODEFORMER-CONSTRAINTS.txt" } else { $doc }
            Copy-Item -LiteralPath $docPath -Destination (Join-Path $aiRoot $targetName) -Force
        }
    }

    $previousPythonPath = $env:PYTHONPATH
    $env:PYTHONPATH = $stagedRepository
    try {
        & (Join-Path $portablePython "python.exe") -c "import cv2, torch, basicsr; from facelib.utils.face_restoration_helper import FaceRestoreHelper"
        if ($LASTEXITCODE -ne 0) {
            throw "Packaged Windows CodeFormer runtime cannot import cv2, torch, basicsr and facelib."
        }
    } finally {
        $env:PYTHONPATH = $previousPythonPath
    }

    $licensePath = Join-Path $repoRoot "tools\REALESRGAN-LICENSE"
    if (Test-Path -LiteralPath $licensePath) {
        Copy-Item -LiteralPath $licensePath -Destination (Join-Path $aiRoot "REALESRGAN-LICENSE") -Force
    }
    Set-Content -LiteralPath (Join-Path $aiRoot "AI-PACKAGE-VERSION") -Value $Version -Encoding ASCII
    Write-AiPackageScripts -PackageRoot $aiRoot
}

if (!$NoZip) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    if ($buildMainPackage) {
        Log "creating main zip: $zipPath"
        if (Test-Path -LiteralPath $zipPath) {
            Remove-Item -LiteralPath $zipPath -Force
        }
        Compress-Archive -LiteralPath $appRoot -DestinationPath $zipPath -Force
    }
    if ($buildAiPackage) {
        Log "creating AI zip: $aiZipPath"
        if (Test-Path -LiteralPath $aiZipPath) {
            Remove-Item -LiteralPath $aiZipPath -Force
        }
        Compress-Archive -LiteralPath $aiRoot -DestinationPath $aiZipPath -Force
    }
}

$issPath = ""
$aiIssPath = ""
if (!$NoInstaller) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    if ($buildMainPackage) {
        Log "creating main installer script"
        $issPath = Write-InnoSetupScript `
            -PackageRoot $appRoot `
            -OutputDir $OutputDir `
            -PackageName $packageName `
            -Version $Version `
            -Platform $Platform `
            -Configuration $Configuration
    }
    if ($buildAiPackage) {
        Log "creating AI installer script"
        $aiIssPath = Write-AiInnoSetupScript `
            -PackageRoot $aiRoot `
            -OutputDir $OutputDir `
            -PackageName $aiPackageName `
            -Version $Version `
            -Platform $Platform `
            -Configuration $Configuration
    }

    $iscc = Find-InnoSetup -ExplicitPath $InnoSetupPath
    if ($iscc) {
        if ($buildMainPackage) {
            Log "building main installer with Inno Setup"
            if (Test-Path -LiteralPath $setupPath) {
                Remove-Item -LiteralPath $setupPath -Force
            }
            & $iscc $issPath
            if (!(Test-Path -LiteralPath $setupPath)) {
                throw "installer was not created: $setupPath"
            }
        }
        if ($buildAiPackage) {
            Log "building AI installer with Inno Setup"
            if (Test-Path -LiteralPath $aiSetupPath) {
                Remove-Item -LiteralPath $aiSetupPath -Force
            }
            & $iscc $aiIssPath
            if (!(Test-Path -LiteralPath $aiSetupPath)) {
                throw "AI installer was not created: $aiSetupPath"
            }
        }
    } else {
        Write-Warning "Inno Setup compiler (ISCC.exe) was not found. Install Inno Setup 6 or pass -InnoSetupPath to build the setup EXE."
        if ($buildMainPackage) { Write-Warning "Main installer script was generated: $issPath" }
        if ($buildAiPackage) { Write-Warning "AI installer script was generated: $aiIssPath" }
    }
}

Log "done"
if ($buildMainPackage) {
    Write-Host "Main package directory: $appRoot"
    if (!$NoZip) { Write-Host "Main package zip:       $zipPath" }
    if (!$NoInstaller) {
        Write-Host "Main installer script:  $issPath"
        if (Test-Path -LiteralPath $setupPath) { Write-Host "Main installer exe:     $setupPath" }
    }
}
if ($buildAiPackage) {
    Write-Host "AI package directory:   $aiRoot"
    if (!$NoZip) { Write-Host "AI package zip:         $aiZipPath" }
    if (!$NoInstaller) {
        Write-Host "AI installer script:    $aiIssPath"
        if (Test-Path -LiteralPath $aiSetupPath) { Write-Host "AI installer exe:       $aiSetupPath" }
    }
}
