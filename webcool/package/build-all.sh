#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

TARGET="all"
ARGS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --target)
      TARGET="$2"
      shift 2
      ;;
    *)
      ARGS+=("$1")
      shift
      ;;
  esac
done

run_one() {
  local script_name="$1"
  shift
  "${SCRIPT_DIR}/${script_name}" "$@"
}

case "$TARGET" in
  mac)
    run_one build-mac.sh "${ARGS[@]}"
    ;;
  deb)
    run_one build-deb.sh "${ARGS[@]}"
    ;;
  rpm)
    run_one build-rpm.sh "${ARGS[@]}"
    ;;
  all)
    run_one build-mac.sh "${ARGS[@]}" || true
    run_one build-deb.sh "${ARGS[@]}" || true
    run_one build-rpm.sh "${ARGS[@]}" || true
    ;;
  *)
    echo "unsupported target: ${TARGET}" >&2
    echo "use --target mac|deb|rpm|all" >&2
    exit 1
    ;;
esac
