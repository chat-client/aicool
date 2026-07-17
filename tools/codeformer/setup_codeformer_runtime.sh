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
(
  cd "$CODEFORMER_ROOT"
  "${VENV_ROOT}/bin/python" -m pip install --no-deps ./basicsr
)
printf 'CodeFormer runtime ready: %s\n' "$VENV_ROOT"
