#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/mac"
APP_SOURCE_PATH="${SCRIPT_DIR}/../webcool.app"
APP_INSTALL_PATH="/Applications/webcool.app"

VERSION="${WEBCOOL_PACKAGE_VERSION:-2.0.0}"
SIGN_APP_IDENTITY="${WEBCOOL_MACOS_SIGN_APP_IDENTITY:-F75A4786D88240E4B651D717C930D5F1E5B0CED4}"
SIGN_INSTALLER_IDENTITY="${WEBCOOL_MACOS_SIGN_INSTALLER_IDENTITY:-839AE2544C625D4D915BB495D9901217E76374BB}"
NOTARY_PROFILE="${WEBCOOL_NOTARY_PROFILE:-webcool-notary}"
ARCH_SUFFIX="universal"
VOLUME_NAME="webcool ${VERSION} Installer"

PKG_PATH="${OUTPUT_DIR}/webcool-${VERSION}-macos-${ARCH_SUFFIX}.pkg"
DMG_PATH="${OUTPUT_DIR}/webcool-${VERSION}-macos-${ARCH_SUFFIX}.dmg"

log() {
  printf '[build-mac-main] %s\n' "$*"
}

require_cmd() {
  local command_name="$1"
  if ! command -v "$command_name" >/dev/null 2>&1; then
    printf 'missing command: %s\n' "$command_name" >&2
    exit 1
  fi
}

notarize_and_staple_dmg() {
  local dmg_path="$1"

  log "submitting DMG for notarization: ${dmg_path}"
  xcrun notarytool submit "$dmg_path" \
    --keychain-profile "$NOTARY_PROFILE" \
    --wait

  log "stapling notarization ticket to DMG"
  xcrun stapler staple "$dmg_path"
  xcrun stapler validate "$dmg_path"
}

verify_gui_app_in_pkg() {
  local pkg_path="$1"
  local expanded_pkg
  local package_info

  if ! pkgutil --payload-files "$pkg_path" |
    grep -E '^(\./)?Applications/webcool\.app(/|$)' >/dev/null; then
    printf 'GUI application is missing from package payload: %s\n' "$pkg_path" >&2
    printf 'expected installation target: %s\n' "$APP_INSTALL_PATH" >&2
    exit 1
  fi

  expanded_pkg="$(mktemp -d "${TMPDIR:-/tmp}/webcool-pkg-check.XXXXXX")"
  if ! pkgutil --expand "$pkg_path" "${expanded_pkg}/expanded"; then
    rm -rf "$expanded_pkg"
    printf 'could not inspect package metadata: %s\n' "$pkg_path" >&2
    exit 1
  fi
  package_info="${expanded_pkg}/expanded/PackageInfo"

  if grep -q '<relocate>' "$package_info"; then
    rm -rf "$expanded_pkg"
    printf 'GUI application is still relocatable in package: %s\n' "$pkg_path" >&2
    printf 'it must always install into: %s\n' "$APP_INSTALL_PATH" >&2
    exit 1
  fi
  rm -rf "$expanded_pkg"

  log "verified GUI application install target: ${APP_INSTALL_PATH}"
}

require_cmd hdiutil
require_cmd codesign
require_cmd pkgutil
require_cmd xcrun

mkdir -p "$OUTPUT_DIR"

if [ ! -d "$APP_SOURCE_PATH" ]; then
  printf 'GUI application bundle was not generated: %s\n' "$APP_SOURCE_PATH" >&2
  printf 'the main package must install webcool.app into %s\n' "$APP_INSTALL_PATH" >&2
  exit 1
fi

log "building, signing and notarizing the main PKG"
"${SCRIPT_DIR}/build-mac.sh" \
  --version "$VERSION" \
  --sign-app-identity "$SIGN_APP_IDENTITY" \
  --sign-installer-identity "$SIGN_INSTALLER_IDENTITY" \
  --notarize \
  --notary-profile "$NOTARY_PROFILE" \
  --universal \
  --main-only

if [ ! -f "$PKG_PATH" ]; then
  printf 'signed PKG was not generated: %s\n' "$PKG_PATH" >&2
  exit 1
fi

verify_gui_app_in_pkg "$PKG_PATH"

temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/webcool-dmg.XXXXXX")"
trap 'rm -rf "$temp_dir"' EXIT HUP INT TERM

dmg_root="${temp_dir}/dmg-root"
mkdir -p "$dmg_root"
cp "$PKG_PATH" "${dmg_root}/Install webcool.pkg"

cat > "${dmg_root}/安装说明.txt" <<EOF
webcool ${VERSION} 安装说明

1. 双击 “Install webcool.pkg”。
2. 按照 macOS 安装器中的步骤完成安装。
3. 安装完成后，从“应用程序”目录启动 webcool。

命令行入口：/usr/local/bin/webcool
应用程序：/Applications/webcool.app
运行资源：/opt/soft/webcool

Installation

1. Double-click “Install webcool.pkg”.
2. Follow the steps in the macOS Installer.
3. Launch webcool from the Applications folder after installation.
EOF

log "creating DMG containing the notarized PKG"
rm -f "$DMG_PATH"
hdiutil create \
  -volname "$VOLUME_NAME" \
  -srcfolder "$dmg_root" \
  -format UDZO \
  -imagekey zlib-level=9 \
  -ov \
  "$DMG_PATH"

log "signing DMG with Developer ID Application"
codesign \
  --force \
  --timestamp \
  --sign "$SIGN_APP_IDENTITY" \
  "$DMG_PATH"

codesign --verify --verbose=2 "$DMG_PATH"
hdiutil verify "$DMG_PATH"

notarize_and_staple_dmg "$DMG_PATH"

log "PKG: ${PKG_PATH} ($(du -h "$PKG_PATH" | awk '{print $1}'))"
log "DMG: ${DMG_PATH} ($(du -h "$DMG_PATH" | awk '{print $1}'))"
