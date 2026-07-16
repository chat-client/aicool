#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

VERSION="${DEFAULT_VERSION}"
IDENTIFIER="com.webcool.server"
AI_IDENTIFIER=""
SKIP_BUILD=0
BUILD_MAIN_PACKAGE=1
BUILD_AI_PACKAGE=1
LIST_IDENTITIES=0
STORE_NOTARY_PROFILE=""
SIGN_APP_IDENTITY="${WEBCOOL_MACOS_SIGN_APP_IDENTITY:-}"
SIGN_INSTALLER_IDENTITY="${WEBCOOL_MACOS_SIGN_INSTALLER_IDENTITY:-}"
NOTARIZE=0
NOTARY_PROFILE="${WEBCOOL_NOTARY_PROFILE:-}"
NOTARY_APPLE_ID="${WEBCOOL_NOTARY_APPLE_ID:-}"
NOTARY_TEAM_ID="${WEBCOOL_NOTARY_TEAM_ID:-}"
NOTARY_PASSWORD="${WEBCOOL_NOTARY_PASSWORD:-}"
UNIVERSAL="${WEBCOOL_MACOS_UNIVERSAL:-0}"

usage() {
  cat <<'EOF'
Usage: build-mac.sh [options]

Build separate macOS .pkg installers for webcool and its optional AI assets.

Options:
  --version VERSION                 Package version (default: from webcool -v)
  --identifier IDENTIFIER           Package bundle identifier
  --ai-identifier IDENTIFIER        AI package identifier (default: <identifier>.ai-models)
  --skip-build                      Skip "make all" before packaging
  --main-only                       Build only the main webcool package
  --ai-only                         Build only the optional AI models package
  --universal                       Build arm64+x86_64 universal binaries and pkg
  --list-signing-identities         Show code signing identities in Keychain
  --sign-app-identity NAME          Developer ID Application identity
  --sign-installer-identity NAME    Developer ID Installer identity
  --notarize                        Notarize the package with Apple
  --notary-profile PROFILE          Keychain profile for notarytool
  --notary-apple-id EMAIL           Apple ID for notarytool
  --notary-team-id TEAMID           Apple Team ID for notarytool
  --notary-password PASSWORD        App-specific password for notarytool
  --store-notary-profile PROFILE    Save notary credentials to Keychain

Environment variables:
  WEBCOOL_MACOS_SIGN_APP_IDENTITY
  WEBCOOL_MACOS_SIGN_INSTALLER_IDENTITY
  WEBCOOL_NOTARY_PROFILE
  WEBCOOL_NOTARY_APPLE_ID
  WEBCOOL_NOTARY_TEAM_ID
  WEBCOOL_NOTARY_PASSWORD
  WEBCOOL_MACOS_UNIVERSAL           Set to 1 to enable --universal by default

Distribution notes:
  - "Apple Development" certificates cannot install on arbitrary Macs.
  - You need paid Apple Developer Program membership.
  - Create "Developer ID Application" and "Developer ID Installer" certs
    at https://developer.apple.com/account/resources/certificates/list
  - macOS 10.15+ also requires notarization for smooth installation.

Examples:
  ./build-mac.sh --version 1.0.0 --universal \
    --sign-app-identity "Developer ID Application: Your Name (TEAMID)" \
    --sign-installer-identity "Developer ID Installer: Your Name (TEAMID)" \
    --notarize --notary-profile webcool-notary

  ./build-mac.sh --version 1.0.0 \
    --sign-app-identity "Developer ID Application: Your Name (TEAMID)" \
    --sign-installer-identity "Developer ID Installer: Your Name (TEAMID)"

Default outputs:
  mac/webcool-<version>-macos-<arch>.pkg
  mac/webcool-ai-models-<version>-macos-<arch>.pkg

Install the main package first. Install the AI models package only on machines
that need CodeFormer, Core ML, Real-ESRGAN, Restormer or red-eye correction.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --version)
      VERSION="$2"
      shift 2
      ;;
    --identifier)
      IDENTIFIER="$2"
      shift 2
      ;;
    --ai-identifier)
      AI_IDENTIFIER="$2"
      shift 2
      ;;
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    --main-only)
      BUILD_MAIN_PACKAGE=1
      BUILD_AI_PACKAGE=0
      shift
      ;;
    --ai-only)
      BUILD_MAIN_PACKAGE=0
      BUILD_AI_PACKAGE=1
      shift
      ;;
    --universal)
      UNIVERSAL=1
      shift
      ;;
    --list-signing-identities)
      LIST_IDENTITIES=1
      shift
      ;;
    --sign-app-identity)
      SIGN_APP_IDENTITY="$2"
      shift 2
      ;;
    --sign-installer-identity)
      SIGN_INSTALLER_IDENTITY="$2"
      shift 2
      ;;
    --notarize)
      NOTARIZE=1
      shift
      ;;
    --notary-profile)
      NOTARY_PROFILE="$2"
      shift 2
      ;;
    --notary-apple-id)
      NOTARY_APPLE_ID="$2"
      shift 2
      ;;
    --notary-team-id)
      NOTARY_TEAM_ID="$2"
      shift 2
      ;;
    --notary-password)
      NOTARY_PASSWORD="$2"
      shift 2
      ;;
    --store-notary-profile)
      STORE_NOTARY_PROFILE="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [ -z "$AI_IDENTIFIER" ]; then
  AI_IDENTIFIER="${IDENTIFIER}.ai-models"
fi

if ! is_macos_host; then
  printf 'build-mac.sh must run on macOS\n' >&2
  exit 1
fi

if [ "$LIST_IDENTITIES" -eq 1 ]; then
  list_macos_signing_identities
  exit 0
fi

if [ -n "$STORE_NOTARY_PROFILE" ]; then
  store_macos_notary_credentials \
    "$STORE_NOTARY_PROFILE" \
    "$NOTARY_APPLE_ID" \
    "$NOTARY_TEAM_ID" \
    "$NOTARY_PASSWORD"
  exit 0
fi

SIGN_APP_IDENTITY="$(resolve_macos_sign_identity \
  "$SIGN_APP_IDENTITY" \
  "${WEBCOOL_MACOS_SIGN_APP_IDENTITY:-}" \
  "Developer ID Application")"
SIGN_INSTALLER_IDENTITY="$(resolve_macos_sign_identity \
  "$SIGN_INSTALLER_IDENTITY" \
  "${WEBCOOL_MACOS_SIGN_INSTALLER_IDENTITY:-}" \
  "Developer ID Installer")"

assert_macos_signing_identity "$SIGN_APP_IDENTITY" "Developer ID Application"
assert_macos_signing_identity "$SIGN_INSTALLER_IDENTITY" "Developer ID Installer"

if [ "$NOTARIZE" -eq 1 ] && { [ -z "$SIGN_APP_IDENTITY" ] || [ -z "$SIGN_INSTALLER_IDENTITY" ]; }; then
  printf 'notarization requires Developer ID Application and Developer ID Installer identities\n' >&2
  printf 'run: ./build-mac.sh --list-signing-identities\n' >&2
  exit 1
fi

if [ "$SKIP_BUILD" -eq 0 ]; then
  build_webcool_binary "$UNIVERSAL"
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

mkdir -p "${PACKAGE_ROOT}/mac"
pkg_arch="$(macos_pkg_arch_suffix "$UNIVERSAL")"
out_pkg="${PACKAGE_ROOT}/mac/webcool-${VERSION}-macos-${pkg_arch}.pkg"
ai_out_pkg="${PACKAGE_ROOT}/mac/webcool-ai-models-${VERSION}-macos-${pkg_arch}.pkg"

if [ "$BUILD_MAIN_PACKAGE" -eq 1 ]; then
  main_root="${tmp_dir}/main-root"
  stage_runtime_tree "$main_root" 0
  prune_macos_package_payload "${main_root}${INSTALL_PREFIX}"
  sign_macos_payload "$main_root" "$SIGN_APP_IDENTITY"

  build_macos_pkg \
    "$main_root" \
    "$IDENTIFIER" \
    "$VERSION" \
    "$out_pkg" \
    "$SIGN_INSTALLER_IDENTITY"
fi

if [ "$BUILD_AI_PACKAGE" -eq 1 ]; then
  ai_root="${tmp_dir}/ai-root"
  stage_ai_runtime_tree "$ai_root" "$VERSION"
  verify_macos_ai_payload "${ai_root}${INSTALL_PREFIX}"
  prune_macos_package_payload "${ai_root}${INSTALL_PREFIX}"
  sign_macos_payload "$ai_root" "$SIGN_APP_IDENTITY"

  build_macos_pkg \
    "$ai_root" \
    "$AI_IDENTIFIER" \
    "$VERSION" \
    "$ai_out_pkg" \
    "$SIGN_INSTALLER_IDENTITY"
fi

if [ "$NOTARIZE" -eq 1 ]; then
  if [ "$BUILD_MAIN_PACKAGE" -eq 1 ]; then
    notarize_macos_pkg \
      "$out_pkg" \
      "$NOTARY_PROFILE" \
      "$NOTARY_APPLE_ID" \
      "$NOTARY_TEAM_ID" \
      "$NOTARY_PASSWORD"
  fi
  if [ "$BUILD_AI_PACKAGE" -eq 1 ]; then
    notarize_macos_pkg \
      "$ai_out_pkg" \
      "$NOTARY_PROFILE" \
      "$NOTARY_APPLE_ID" \
      "$NOTARY_TEAM_ID" \
      "$NOTARY_PASSWORD"
  fi
fi

if [ "$BUILD_MAIN_PACKAGE" -eq 1 ]; then
  log "main package: ${out_pkg} ($(du -h "$out_pkg" | awk '{print $1}'))"
fi
if [ "$BUILD_AI_PACKAGE" -eq 1 ]; then
  log "AI models package: ${ai_out_pkg} ($(du -h "$ai_out_pkg" | awk '{print $1}'))"
fi
