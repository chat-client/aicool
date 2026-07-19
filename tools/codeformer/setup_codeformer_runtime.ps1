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
if ($LASTEXITCODE -ne 0) {
    throw "Failed to create CodeFormer virtual environment."
}
$VenvPython = Join-Path $VenvRoot "Scripts\python.exe"
& $VenvPython -m pip install --upgrade pip
if ($LASTEXITCODE -ne 0) {
    throw "Failed to upgrade pip in the CodeFormer virtual environment."
}
& $VenvPython -m pip install -c $Constraints -r (Join-Path $CodeFormerRoot "requirements.txt") cython
if ($LASTEXITCODE -ne 0) {
    throw "Failed to install CodeFormer Python dependencies."
}
& $VenvPython (Join-Path $ScriptDir "prepare_codeformer_source.py")
if ($LASTEXITCODE -ne 0) {
    throw "Failed to generate the BasicSR version module."
}

$VerifyScript = Join-Path $VenvRoot "verify_codeformer_runtime.py"
[System.IO.File]::WriteAllText($VerifyScript, @'
import cv2
import torch
import torchvision
import basicsr
from basicsr.utils import imwrite
from facelib.utils.face_restoration_helper import FaceRestoreHelper

print("CodeFormer imports verified:", torch.__version__, torchvision.__version__)
'@, [System.Text.Encoding]::UTF8)

Push-Location $CodeFormerRoot
$PreviousPythonPath = $env:PYTHONPATH
try {
    # BasicSR and FaceLib are vendored directly in the CodeFormer repository.
    # The runtime adds this directory to PYTHONPATH, so no separate install is required.
    if ($PreviousPythonPath) {
        $env:PYTHONPATH = "$CodeFormerRoot;$PreviousPythonPath"
    } else {
        $env:PYTHONPATH = $CodeFormerRoot
    }
    & $VenvPython $VerifyScript
    if ($LASTEXITCODE -ne 0) {
        throw "CodeFormer dependency import verification failed."
    }
} finally {
    $env:PYTHONPATH = $PreviousPythonPath
    Pop-Location
}

Write-Host "CodeFormer Windows runtime ready: $VenvRoot"
