#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

VERSION="${DEFAULT_VERSION}"
RELEASE="${DEFAULT_RELEASE}"
MAINTAINER="webcool"
SKIP_BUILD=0
BUILD_MAIN_PACKAGE=1
BUILD_AI_PACKAGE=1

while [ $# -gt 0 ]; do
  case "$1" in
    --version)
      VERSION="$2"
      shift 2
      ;;
    --release)
      RELEASE="$2"
      shift 2
      ;;
    --maintainer)
      MAINTAINER="$2"
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
    *)
      echo "unknown option: $1" >&2
      exit 1
      ;;
  esac
done

require_cmd dpkg-deb

if [ "$SKIP_BUILD" -eq 0 ] && [ "$BUILD_MAIN_PACKAGE" -eq 1 ]; then
  build_webcool_binary
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

deb_arch="$(map_deb_arch)"
mkdir -p "${PACKAGE_ROOT}/deb"
out_deb="${PACKAGE_ROOT}/deb/webcool_${VERSION}-${RELEASE}_${deb_arch}.deb"
ai_out_deb="${PACKAGE_ROOT}/deb/webcool-ai-models_${VERSION}-${RELEASE}_${deb_arch}.deb"

if [ "$BUILD_MAIN_PACKAGE" -eq 1 ]; then
  pkg_root="${tmp_dir}/main-pkg"
  stage_runtime_tree "$pkg_root" 0
  mkdir -p "${pkg_root}/DEBIAN"
  cat > "${pkg_root}/DEBIAN/control" <<EOF
Package: webcool
Version: ${VERSION}-${RELEASE}
Section: utils
Priority: optional
Architecture: ${deb_arch}
Maintainer: ${MAINTAINER}
Depends: libc6
Description: webcool private file management console
 webcool is a browser based private file manager with media preview,
 tag tree, lock system, recycle bin and admin settings.
EOF

  log "building Ubuntu main package: ${out_deb}"
  dpkg-deb --build "$pkg_root" "$out_deb"
fi

if [ "$BUILD_AI_PACKAGE" -eq 1 ]; then
  ai_pkg_root="${tmp_dir}/ai-pkg"
  stage_ai_runtime_tree "$ai_pkg_root" "$VERSION"
  verify_linux_ai_payload "${ai_pkg_root}${INSTALL_PREFIX}"
  mkdir -p "${ai_pkg_root}/DEBIAN"
  cat > "${ai_pkg_root}/DEBIAN/control" <<EOF
Package: webcool-ai-models
Version: ${VERSION}-${RELEASE}
Section: utils
Priority: optional
Architecture: ${deb_arch}
Maintainer: ${MAINTAINER}
Depends: webcool (>= ${VERSION}-${RELEASE}), python3
Description: optional AI runtimes and models for webcool
 This package adds CodeFormer and Real-ESRGAN runtimes and models to webcool.
 It does not contain the main webcool server.
EOF

  log "building Ubuntu AI models package: ${ai_out_deb}"
  dpkg-deb --build "$ai_pkg_root" "$ai_out_deb"
fi

if [ "$BUILD_MAIN_PACKAGE" -eq 1 ]; then
  log "main package: ${out_deb} ($(du -h "$out_deb" | awk '{print $1}'))"
fi
if [ "$BUILD_AI_PACKAGE" -eq 1 ]; then
  log "AI models package: ${ai_out_deb} ($(du -h "$ai_out_deb" | awk '{print $1}'))"
fi
