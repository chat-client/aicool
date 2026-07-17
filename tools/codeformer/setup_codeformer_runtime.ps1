param(
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $PSCommandPath
$CodeFormerRoot = Join-Path $ScriptDir "CodeFormer"
$VenvRoot = Join-Path $ScriptDir "venv\windows"
$Constraints = Join-Path $ScriptDir "codeformer-constraints.txt"

if (!(Test-Path -LiteralPath (Join-Path $CodeFormerRoot "inference_codeformer.py"))) {
    throw "CodeFormer source not found: $CodeFormerRoot"
}
& $Python -m venv $VenvRoot
$VenvPython = Join-Path $VenvRoot "Scripts\python.exe"
& $VenvPython -m pip install --upgrade pip
& $VenvPython -m pip install -c $Constraints -r (Join-Path $CodeFormerRoot "requirements.txt") cython

Push-Location $CodeFormerRoot
try {
    # BasicSR and FaceLib are vendored directly in the CodeFormer repository.
    # The runtime adds this directory to PYTHONPATH, so no separate install is required.
    & $VenvPython -c 'import cv2, torch, torchvision, basicsr; from basicsr.utils import imwrite; from facelib.utils.face_restoration_helper import FaceRestoreHelper; print("CodeFormer imports verified:", torch.__version__, torchvision.__version__)'
    if ($LASTEXITCODE -ne 0) {
        throw "CodeFormer dependency import verification failed."
    }
} finally {
    Pop-Location
}

Write-Host "CodeFormer Windows runtime ready: $VenvRoot"
