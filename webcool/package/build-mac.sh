#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

VERSION="${DEFAULT_VERSION}"
IDENTIFIER="com.webcool.server"
SKIP_BUILD=0
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

Build a macOS .pkg installer for webcool.

Options:
  --version VERSION                 Package version (default: from webcool -v)
  --identifier IDENTIFIER           Package bundle identifier
  --skip-build                      Skip "make all" before packaging
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
    --skip-build)
      SKIP_BUILD=1
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

stage_runtime_tree "${tmp_dir}/root"
prune_macos_package_payload "${tmp_dir}/root${INSTALL_PREFIX}"
sign_macos_payload "${tmp_dir}/root" "$SIGN_APP_IDENTITY"

mkdir -p "${PACKAGE_ROOT}/mac"
pkg_arch="$(macos_pkg_arch_suffix "$UNIVERSAL")"
out_pkg="${PACKAGE_ROOT}/mac/webcool-${VERSION}-macos-${pkg_arch}.pkg"

build_macos_pkg \
  "${tmp_dir}/root" \
  "${IDENTIFIER}" \
  "${VERSION}" \
  "${out_pkg}" \
  "${SIGN_INSTALLER_IDENTITY}"

if [ "$NOTARIZE" -eq 1 ]; then
  notarize_macos_pkg \
    "${out_pkg}" \
    "${NOTARY_PROFILE}" \
    "${NOTARY_APPLE_ID}" \
    "${NOTARY_TEAM_ID}" \
    "${NOTARY_PASSWORD}"
fi

log "done: ${out_pkg}"
