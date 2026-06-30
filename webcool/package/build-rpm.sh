#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

VERSION="${DEFAULT_VERSION}"
RELEASE="${DEFAULT_RELEASE}"
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

require_cmd rpmbuild
require_cmd tar

if [ "$SKIP_BUILD" -eq 0 ]; then
  build_webcool_binary
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

payload_root="${tmp_dir}/payload"
stage_runtime_tree "$payload_root"

topdir="${tmp_dir}/rpmbuild"
mkdir -p "${topdir}/BUILD" "${topdir}/RPMS" "${topdir}/SOURCES" "${topdir}/SPECS" "${topdir}/SRPMS"

tar -C "$payload_root" -czf "${topdir}/SOURCES/webcool-root.tar.gz" .

arch="$(uname -m)"
spec_file="${topdir}/SPECS/webcool.spec"
cat > "$spec_file" <<EOF
Name:           webcool
Version:        ${VERSION}
Release:        ${RELEASE}%{?dist}
Summary:        webcool private file management console
License:        MIT
URL:            https://example.invalid/webcool
Source0:        webcool-root.tar.gz
BuildArch:      ${arch}

%description
webcool is a browser based private file manager with media preview,
tag tree, lock system, recycle bin and admin settings.

%prep
%setup -q -c -T
tar -xzf %{SOURCE0}

%build

%install
mkdir -p %{buildroot}
cp -a opt %{buildroot}/
cp -a usr %{buildroot}/

%files
/opt/soft/webcool
/usr/local/bin/webcool

%changelog
* Thu May 21 2026 webcool packager - ${VERSION}-${RELEASE}
- Initial RPM package
EOF

log "building rpm package"
rpmbuild -bb --define "_topdir ${topdir}" --define "_build_id_links none" "$spec_file"

mkdir -p "${PACKAGE_ROOT}/rpm"
rpm_file="$(find "${topdir}/RPMS" -type f -name '*.rpm' | head -n 1)"
if [ -z "$rpm_file" ]; then
  echo "rpm build failed: no rpm output found" >&2
  exit 1
fi

out_rpm="${PACKAGE_ROOT}/rpm/webcool-${VERSION}-${RELEASE}.${arch}.rpm"
cp -f "$rpm_file" "$out_rpm"
log "done: ${out_rpm}"
