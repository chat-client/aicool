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
    & $VenvPython -m pip install --no-deps ".\basicsr"
} finally {
    Pop-Location
}

Write-Host "CodeFormer Windows runtime ready: $VenvRoot"
