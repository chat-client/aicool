#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "${SCRIPT_DIR}/CodeFormer/inference_codeformer.py" ]; then
  CODEFORMER_HOME="$SCRIPT_DIR"
  case "$(uname -s)" in
    Darwin) VENV_ROOT="${CODEFORMER_HOME}/venv/mac" ;;
    Linux) VENV_ROOT="${CODEFORMER_HOME}/venv/linux" ;;
    *)
      printf 'Unsupported operating system. On Windows run setup_codeformer_runtime.ps1.\n' >&2
      exit 1
      ;;
  esac
else
  CODEFORMER_HOME="${SCRIPT_DIR}/codeformer"
  # An installed AI package contains only its target platform, so its venv is flat.
  VENV_ROOT="${CODEFORMER_HOME}/venv"
fi
CODEFORMER_ROOT="${CODEFORMER_HOME}/CodeFormer"
CONSTRAINTS="${SCRIPT_DIR}/codeformer-constraints.txt"
if [ ! -f "$CONSTRAINTS" ]; then
  CONSTRAINTS="${SCRIPT_DIR}/CODEFORMER-CONSTRAINTS.txt"
fi

if [ ! -f "${CODEFORMER_ROOT}/inference_codeformer.py" ]; then
  printf 'CodeFormer source not found: %s\n' "$CODEFORMER_ROOT" >&2
  exit 1
fi

python3 -m venv "$VENV_ROOT"
"${VENV_ROOT}/bin/python" -m pip install --upgrade pip
"${VENV_ROOT}/bin/python" -m pip install \
  -c "$CONSTRAINTS" \
  -r "${CODEFORMER_ROOT}/requirements.txt" cython
"${VENV_ROOT}/bin/python" "${SCRIPT_DIR}/prepare_codeformer_source.py"
(
  cd "$CODEFORMER_ROOT"
  # BasicSR and FaceLib are vendored directly in the CodeFormer repository.
  # The runtime adds this directory to PYTHONPATH, so installing BasicSR as a
  # wheel/editable package is unnecessary and would embed checkout paths.
  "${VENV_ROOT}/bin/python" -c \
    'import cv2, torch, torchvision, basicsr; from basicsr.utils import imwrite; from facelib.utils.face_restoration_helper import FaceRestoreHelper; print("CodeFormer imports verified:", torch.__version__, torchvision.__version__)'
)
printf 'CodeFormer runtime ready: %s\n' "$VENV_ROOT"
