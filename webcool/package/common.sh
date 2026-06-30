#!/usr/bin/env bash

set -euo pipefail

PACKAGE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEBCOOL_ROOT="$(cd "${PACKAGE_ROOT}/.." && pwd)"
PROJECT_ROOT="$(cd "${WEBCOOL_ROOT}/.." && pwd)"
ACL_ROOT="${PROJECT_ROOT}/third-party/acl"
SQLITE_ROOT="${PROJECT_ROOT}/third-party/sqlite"
TOOLS_ROOT="${PROJECT_ROOT}/tools"

# Unix 安装根目录（deb/rpm/mac pkg 共用）
INSTALL_PREFIX="/opt/soft/webcool"

detect_default_version() {
  local fallback="1.0.0"
  local bin_path="${WEBCOOL_ROOT}/webcool"
  local ver

  if [ -x "$bin_path" ]; then
    ver="$($bin_path -v 2>/dev/null | head -n 1 | tr -d '[:space:]')"
    if [ -n "$ver" ]; then
      printf '%s\n' "$ver"
      return 0
    fi
  fi

  printf '%s\n' "$fallback"
}

DEFAULT_VERSION="$(detect_default_version)"
DEFAULT_RELEASE="1"

log() {
  printf '[package] %s\n' "$*"
}

require_cmd() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    printf 'missing command: %s\n' "$cmd" >&2
    exit 1
  fi
}

macos_pkg_arch_suffix() {
  local universal="$1"
  if [ "$universal" -eq 1 ]; then
    printf 'universal\n'
  else
    uname -m
  fi
}

verify_macos_universal_binary() {
  local bin_path="$1"
  local label="$2"

  require_cmd lipo
  if [ ! -f "$bin_path" ]; then
    printf '%s not found: %s\n' "$label" "$bin_path" >&2
    exit 1
  fi

  local archs
  archs="$(lipo -archs "$bin_path" 2>/dev/null || true)"
  if printf '%s' "$archs" | grep -q 'arm64' \
    && printf '%s' "$archs" | grep -q 'x86_64'; then
    return 0
  fi

  log "error: ${label} must contain both arm64 and x86_64 (archs: ${archs:-unknown})" >&2
  exit 1
}

warn_macos_non_universal_ffmpeg() {
  local ffmpeg_src="${TOOLS_ROOT}/mac/ffmpeg"

  if [ ! -f "$ffmpeg_src" ]; then
    return 0
  fi

  require_cmd lipo
  local archs
  archs="$(lipo -archs "$ffmpeg_src" 2>/dev/null || true)"
  if printf '%s' "$archs" | grep -q 'arm64' \
    && printf '%s' "$archs" | grep -q 'x86_64'; then
    return 0
  fi

  log "warning: tools/mac/ffmpeg is not universal (${archs:-unknown})"
  log "warning: transcoding may fail on the other CPU architecture"
}

build_webcool_binary() {
  local universal="${1:-0}"

  if [ "$universal" -eq 1 ]; then
    local arch_flags="-arch arm64 -arch x86_64"

    log "building universal binaries (arm64 + x86_64)"

    log "rebuilding ACL"
    make -C "${ACL_ROOT}" clean
    make -C "${ACL_ROOT}" all_lib \
      ENV_CC="clang ${arch_flags}" \
      ENV_CPP="g++ ${arch_flags}"

    log "rebuilding sqlite"
    make -C "${SQLITE_ROOT}" clean
    make -C "${SQLITE_ROOT}" build MACOS_UNIVERSAL=1

    log "building webcool"
    make -C "${WEBCOOL_ROOT}" clean
    make -C "${WEBCOOL_ROOT}" all MACOS_UNIVERSAL=1

    verify_macos_universal_binary "${WEBCOOL_ROOT}/webcool" "webcool"
    verify_macos_universal_binary "${SQLITE_ROOT}/lib/sqlite3.so" "sqlite3.so"
    log "webcool archs: $(lipo -archs "${WEBCOOL_ROOT}/webcool")"
    log "sqlite archs: $(lipo -archs "${SQLITE_ROOT}/lib/sqlite3.so")"
    warn_macos_non_universal_ffmpeg
    return 0
  fi

  log "building webcool binary (native: $(uname -m))"
  make -C "${WEBCOOL_ROOT}" all MACOS_UNIVERSAL=0
}

copy_if_exists() {
  local src="$1"
  local dst="$2"
  if [ -e "$src" ]; then
    cp -a "$src" "$dst"
  fi
}

copy_acl_runtime_libs() {
  local lib_dst="$1"
  mkdir -p "$lib_dst"

  local lib_dir
  for lib_dir in \
    "${ACL_ROOT}/lib_acl/lib" \
    "${ACL_ROOT}/lib_acl_cpp/lib" \
    "${ACL_ROOT}/lib_protocol/lib" \
    "${ACL_ROOT}/lib_fiber/lib"; do
    if [ -d "$lib_dir" ]; then
      find "$lib_dir" -maxdepth 1 -type f \( -name '*.so' -o -name '*.so.*' -o -name '*.dylib' \) -exec cp -a {} "$lib_dst" \;
    fi
  done
}

copy_sqlite_runtime_lib() {
  local lib_dst="$1"
  local sqlite_src="${SQLITE_ROOT}/lib/sqlite3.so"

  if [ ! -f "$sqlite_src" ]; then
    printf 'sqlite runtime library not found: %s\n' "$sqlite_src" >&2
    exit 1
  fi

  mkdir -p "$lib_dst"
  cp -a "$sqlite_src" "${lib_dst}/sqlite3.so"
}

create_launcher_script() {
  local launch_path="$1"
  local base_dir="${INSTALL_PREFIX:-/opt/soft/webcool}"
  cat > "$launch_path" <<EOF
#!/bin/sh

BASE_DIR="${base_dir}"
WEBCOOL_BIN="\${BASE_DIR}/sbin/webcool"

if [ ! -x "\$WEBCOOL_BIN" ]; then
  echo "webcool binary not found: \$WEBCOOL_BIN" >&2
  exit 127
fi
if [ -d "\$BASE_DIR/lib" ]; then
  export LD_LIBRARY_PATH="\$BASE_DIR/lib:\${LD_LIBRARY_PATH:-}"
  export DYLD_LIBRARY_PATH="\$BASE_DIR/lib:\${DYLD_LIBRARY_PATH:-}"
fi
if [ -x "\$BASE_DIR/bin/ffmpeg" ] && [ -z "\${AICOOL_FFMPEG:-}" ]; then
  export AICOOL_FFMPEG="\$BASE_DIR/bin/ffmpeg"
fi
if [ -f "\$BASE_DIR/lib/sqlite3.so" ] && [ -z "\${AICOOL_SQLITE_LIB:-}" ]; then
  export AICOOL_SQLITE_LIB="\$BASE_DIR/lib/sqlite3.so"
fi

DEFAULT_CONF="\${BASE_DIR}/conf/webcool.cf"
if [ -f "\$DEFAULT_CONF" ]; then
  exec "\$WEBCOOL_BIN" -f "\$DEFAULT_CONF" "\$@"
else
  exec "\$WEBCOOL_BIN" "\$@"
fi
EOF
  chmod 0755 "$launch_path"
}

copy_ffmpeg_runtime_bin() {
  local bin_dst="$1"
  local ffmpeg_src=""

  case "$(uname -s)" in
    Darwin)
      ffmpeg_src="${TOOLS_ROOT}/mac/ffmpeg"
      ;;
    Linux)
      ffmpeg_src="${TOOLS_ROOT}/linux/ffmpeg"
      ;;
    *)
      printf 'unsupported build host for ffmpeg packaging: %s\n' "$(uname -s)" >&2
      exit 1
      ;;
  esac

  if [ ! -f "$ffmpeg_src" ]; then
    printf 'ffmpeg runtime binary not found: %s\n' "$ffmpeg_src" >&2
    exit 1
  fi

  mkdir -p "$bin_dst"
  cp -a "$ffmpeg_src" "${bin_dst}/ffmpeg"
  chmod 0755 "${bin_dst}/ffmpeg"
}

stage_runtime_tree() {
  local stage_root="$1"
  local install_root="${stage_root}${INSTALL_PREFIX}"

  mkdir -p \
    "$install_root/sbin" \
    "$install_root/conf" \
    "$install_root/uploads" \
    "$install_root/lib" \
    "$install_root/bin" \
    "$install_root/var" \
    "$install_root/logs" \
    "${stage_root}/usr/local/bin"

  if [ ! -x "${WEBCOOL_ROOT}/webcool" ]; then
    printf 'webcool binary not found: %s\n' "${WEBCOOL_ROOT}/webcool" >&2
    printf 'run make in webcool directory first, or remove --skip-build\n' >&2
    exit 1
  fi

  cp -a "${WEBCOOL_ROOT}/webcool" "${install_root}/sbin/webcool"
  copy_if_exists "${WEBCOOL_ROOT}/html" "$install_root/"

  if [ ! -f "${WEBCOOL_ROOT}/webcool.cf" ]; then
    printf 'webcool config not found: %s\n' "${WEBCOOL_ROOT}/webcool.cf" >&2
    exit 1
  fi
  cp -a "${WEBCOOL_ROOT}/webcool.cf" "${install_root}/conf/webcool.cf"

  copy_acl_runtime_libs "$install_root/lib"
  copy_sqlite_runtime_lib "$install_root/lib"
  copy_ffmpeg_runtime_bin "$install_root/bin"

  create_launcher_script "${stage_root}/usr/local/bin/webcool"
}

prune_macos_package_payload() {
  local install_root="$1"
  local html_root="${install_root}/html"
  local dir_path
  local zip_path="${html_root}/js/view-heic-browser-extension.zip"

  log "pruning macOS package payload for notarization"

  while IFS= read -r -d '' dir_path; do
    log "removing: ${dir_path#${install_root}/}"
    rm -rf "$dir_path"
  done < <(find "$install_root" -type d -name 'node_modules' -print0 2>/dev/null)

  if [ -f "$zip_path" ]; then
    log "removing: html/js/view-heic-browser-extension.zip (dev archive with unsigned binaries)"
    rm -f "$zip_path"
  fi
}

map_deb_arch() {
  local m
  m="$(uname -m)"
  case "$m" in
    x86_64) echo amd64 ;;
    aarch64|arm64) echo arm64 ;;
    *) echo "$m" ;;
  esac
}

is_macos_host() {
  [ "$(uname -s)" = "Darwin" ]
}

is_mach_o_file() {
  local file_path="$1"
  [ -f "$file_path" ] || return 1
  file -b "$file_path" 2>/dev/null | grep -q 'Mach-O'
}

list_macos_signing_identities() {
  require_cmd security
  log "available signing identities (basic policy; includes Installer certs):"
  security find-identity -v -p basic 2>/dev/null || true
  printf '\n' >&2
  log "for distribution to other Macs you need:"
  log "  - Developer ID Application (sign binaries via codesign)"
  log "  - Developer ID Installer (sign .pkg via productsign)"
  log "  - notarization via Apple notarytool (macOS 10.15+)"
  log "note: Installer certs may not appear under 'security find-identity -p codesigning'."
  log "Apple Development certificates only work on registered devices."
}

find_macos_signing_identity() {
  local kind="$1"
  require_cmd security
  security find-identity -v -p basic 2>/dev/null \
    | sed -n 's/^[[:space:]]*[0-9]*) [0-9A-F]\{40\} "\(.*\)"/\1/p' \
    | awk -v kind="$kind" '$0 ~ kind { print; exit }'
}

macos_signing_identity_exists() {
  local identity="$1"

  if [ -z "$identity" ]; then
    return 1
  fi

  require_cmd security
  security find-identity -v -p basic 2>/dev/null | grep -Fq "$identity"
}

assert_macos_signing_identity() {
  local identity="$1"
  local label="$2"

  if [ -z "$identity" ]; then
    return 0
  fi

  if macos_signing_identity_exists "$identity"; then
    return 0
  fi

  log "error: ${label} identity not found in Keychain: ${identity}" >&2
  log "run: ./build-mac.sh --list-signing-identities" >&2
  log "copy the full name inside quotes exactly (note the space before Team ID)" >&2
  exit 1
}

resolve_macos_sign_identity() {
  local explicit="$1"
  local env_value="$2"
  local kind="$3"
  local resolved=""

  if [ -n "$explicit" ]; then
    resolved="$explicit"
  elif [ -n "$env_value" ]; then
    resolved="$env_value"
  else
    resolved="$(find_macos_signing_identity "$kind" || true)"
  fi

  if [ -n "$resolved" ] && printf '%s' "$resolved" | grep -q '^Apple Development:'; then
    log "warning: '${resolved}' is an Apple Development certificate"
    log "warning: it cannot distribute install packages to arbitrary Macs"
  fi

  printf '%s' "$resolved"
}

sign_macos_payload() {
  local stage_root="$1"
  local app_identity="$2"

  if [ -z "$app_identity" ]; then
    return 0
  fi

  require_cmd codesign
  log "signing Mach-O files with: ${app_identity}"

  local files=()
  local file_path
  while IFS= read -r -d '' file_path; do
    if is_mach_o_file "$file_path"; then
      files+=("$file_path")
    fi
  done < <(find "$stage_root" -type f -print0 | sort -z)

  local depth
  local sorted_files=()
  local f
  for f in "${files[@]}"; do
    depth="$(printf '%s' "$f" | tr -cd '/' | wc -c | tr -d ' ')"
    sorted_files+=("${depth}	${f}")
  done
  IFS=$'\n'
  sorted_files=($(printf '%s\n' "${sorted_files[@]}" | sort -t $'\t' -k1,1nr | cut -f2-))
  unset IFS

  for f in "${sorted_files[@]}"; do
    codesign --force --options runtime --timestamp \
      --sign "$app_identity" "$f"
  done

  log "signed ${#sorted_files[@]} Mach-O file(s)"
}

build_macos_pkg() {
  local stage_root="$1"
  local identifier="$2"
  local version="$3"
  local out_pkg="$4"
  local installer_identity="$5"

  require_cmd pkgbuild
  local unsigned_pkg="${out_pkg}.unsigned"

  log "building macOS package payload: ${out_pkg}"
  pkgbuild \
    --root "$stage_root" \
    --identifier "$identifier" \
    --version "$version" \
    "$unsigned_pkg"

  if [ -n "$installer_identity" ]; then
    require_cmd productsign
    log "signing package with: ${installer_identity}"
    productsign --sign "$installer_identity" "$unsigned_pkg" "$out_pkg"
    rm -f "$unsigned_pkg"
  else
    mv "$unsigned_pkg" "$out_pkg"
    log "warning: package is unsigned; other Macs may block installation"
  fi
}

notarize_macos_pkg() {
  local pkg_path="$1"
  local notary_profile="$2"
  local apple_id="$3"
  local team_id="$4"
  local password="$5"

  require_cmd xcrun
  log "submitting package for notarization: ${pkg_path}"

  if [ -n "$notary_profile" ]; then
    xcrun notarytool submit "$pkg_path" \
      --keychain-profile "$notary_profile" \
      --wait || {
        log "notarization failed; fetch details with:" >&2
        log "  xcrun notarytool history --keychain-profile ${notary_profile}" >&2
        exit 1
      }
  else
    if [ -z "$apple_id" ] || [ -z "$team_id" ] || [ -z "$password" ]; then
      printf 'notarization requires --notary-profile or --notary-apple-id/--notary-team-id/--notary-password\n' >&2
      exit 1
    fi
    xcrun notarytool submit "$pkg_path" \
      --apple-id "$apple_id" \
      --team-id "$team_id" \
      --password "$password" \
      --wait || {
        log "notarization failed; fetch details with:" >&2
        log "  xcrun notarytool history --apple-id ${apple_id} --team-id ${team_id}" >&2
        exit 1
      }
  fi

  xcrun stapler staple "$pkg_path" || {
    log "stapler failed; package may still be notarized but without a stapled ticket" >&2
    exit 1
  }
  log "notarization complete and ticket stapled"
}

store_macos_notary_credentials() {
  local profile_name="$1"
  local apple_id="$2"
  local team_id="$3"
  local password="$4"

  require_cmd xcrun
  if [ -z "$profile_name" ] || [ -z "$apple_id" ] || [ -z "$team_id" ] || [ -z "$password" ]; then
    printf 'usage: store credentials with profile name, apple id, team id, app-specific password\n' >&2
    exit 1
  fi

  xcrun notarytool store-credentials "$profile_name" \
    --apple-id "$apple_id" \
    --team-id "$team_id" \
    --password "$password"
  log "stored notary credentials in keychain profile: ${profile_name}"
}
