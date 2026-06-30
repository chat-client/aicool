#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

VERSION="${DEFAULT_VERSION}"
RELEASE="${DEFAULT_RELEASE}"
MAINTAINER="webcool"
SKIP_BUILD=0

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
    *)
      echo "unknown option: $1" >&2
      exit 1
      ;;
  esac
done

require_cmd dpkg-deb

if [ "$SKIP_BUILD" -eq 0 ]; then
  build_webcool_binary
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

pkg_root="${tmp_dir}/pkg"
stage_runtime_tree "$pkg_root"
mkdir -p "${pkg_root}/DEBIAN"

deb_arch="$(map_deb_arch)"
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

mkdir -p "${PACKAGE_ROOT}/deb"
out_deb="${PACKAGE_ROOT}/deb/webcool_${VERSION}-${RELEASE}_${deb_arch}.deb"

log "building deb package: ${out_deb}"
dpkg-deb --build "$pkg_root" "$out_deb"

log "done: ${out_deb}"
