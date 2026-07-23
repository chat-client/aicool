#!/usr/bin/env bash

set -euo pipefail

PACKAGE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEBCOOL_ROOT="$(cd "${PACKAGE_ROOT}/.." && pwd)"
PROJECT_ROOT="$(cd "${WEBCOOL_ROOT}/.." && pwd)"
ACL_ROOT="${PROJECT_ROOT}/third-party/acl"
SQLITE_ROOT="${PROJECT_ROOT}/third-party/sqlite"
TOOLS_ROOT="${PROJECT_ROOT}/tools"
MODELS_ROOT="${PROJECT_ROOT}/models"

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
# Packaged runtimes must be self-contained.  In particular, do not let an
# incomplete installation silently borrow CodeFormer from a source checkout
# that happens to be the server's working directory.
export AICOOL_PACKAGED_RUNTIME=1

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

copy_realesrgan_runtime() {
  local install_root="$1"
  local platform=""
  case "$(uname -s)" in
    Darwin) platform="mac" ;;
    Linux) platform="linux" ;;
    *) return 0 ;;
  esac
  local source_root="${TOOLS_ROOT}/${platform}"
  local executable="${source_root}/realesrgan-ncnn-vulkan"
  local models="${MODELS_ROOT}/realesrgan/ncnn"
  local missing=0
  if [ "$platform" = "mac" ]; then
    local red_eye_executable="${source_root}/red-eye-correct"
    if [ -x "$red_eye_executable" ]; then
      cp -a "$red_eye_executable" "${install_root}/bin/red-eye-correct"
      chmod 0755 "${install_root}/bin/red-eye-correct"
    else
      log "warning: red-eye correction runtime not found; automatic red-eye removal will be unavailable"
    fi
  fi
  # Source archives and some Git/Gitee transfers can lose the executable bit.
  # It is sufficient for the source to be a regular file because the staged
  # copy is normalized to mode 0755 below.
  if [ ! -f "$executable" ]; then
    log "warning: Real-ESRGAN executable not found: ${executable}"
    missing=1
  fi
  if [ ! -d "$models" ]; then
    log "warning: Real-ESRGAN model directory not found: ${models}"
    missing=1
  else
    local model_file
    for model_file in \
      realesrgan-x4plus.param \
      realesrgan-x4plus.bin \
      realesr-animevideov3-x2.param \
      realesr-animevideov3-x2.bin \
      realesr-animevideov3-x3.param \
      realesr-animevideov3-x3.bin \
      realesr-animevideov3-x4.param \
      realesr-animevideov3-x4.bin; do
      if [ ! -f "${models}/${model_file}" ]; then
        log "warning: Real-ESRGAN model file not found: ${models}/${model_file}"
        missing=1
      fi
    done
  fi
  if [ "$missing" -ne 0 ]; then
    log "warning: Real-ESRGAN runtime is incomplete; AI enhancement will be unavailable"
    log "run: python3 tools/download_realesrgan_models.py"
    return 0
  fi
  cp -a "$executable" "${install_root}/bin/realesrgan-ncnn-vulkan"
  chmod 0755 "${install_root}/bin/realesrgan-ncnn-vulkan"
  mkdir -p "${install_root}/models/realesrgan"
  cp -a "${models}/." "${install_root}/models/realesrgan/"
  copy_if_exists "${TOOLS_ROOT}/REALESRGAN-LICENSE" "${install_root}/"

  if [ "$platform" = "mac" ]; then
    local coreml_executable="${source_root}/coreml-realesrgan"
    local coreml_models="${MODELS_ROOT}/realesrgan/coreml"
    if [ -x "$coreml_executable" ] && [ -d "$coreml_models" ]; then
      cp -a "$coreml_executable" "${install_root}/bin/coreml-realesrgan"
      chmod 0755 "${install_root}/bin/coreml-realesrgan"
      mkdir -p "${install_root}/models/coreml"
      cp -a "${coreml_models}/." "${install_root}/models/coreml/"
    else
      log "warning: Core ML Real-ESRGAN runtime not found; Apple Silicon will use NCNN fallback"
    fi

    local restormer_models="${MODELS_ROOT}/restormer/coreml"
    if [ -d "$restormer_models" ]; then
      mkdir -p "${install_root}/models/restormer"
      cp -a "${restormer_models}/." "${install_root}/models/restormer/"
      copy_if_exists "${TOOLS_ROOT}/RESTORMER-LICENSE" "${install_root}/"
    else
      log "warning: Restormer models not found; deblur-before-upscale will be unavailable"
    fi
  fi
}

copy_codeformer_assets() {
  local install_root="$1"
  local source_root="${TOOLS_ROOT}/codeformer"
  local source="${source_root}/CodeFormer"
  local platform_venv
  case "$(uname -s)" in
    Darwin) platform_venv="mac" ;;
    Linux) platform_venv="linux" ;;
    *)
      printf 'CodeFormer packaging is unsupported on this host: %s\n' "$(uname -s)" >&2
      exit 1
      ;;
  esac
  local venv="${source_root}/venv/${platform_venv}"
  local weights="${MODELS_ROOT}/codeformer/weights"
  local required
  for required in \
    "${source}/inference_codeformer.py" \
    "${source}/inference_inpainting.py" \
    "${weights}/CodeFormer/codeformer.pth" \
    "${weights}/CodeFormer/codeformer_inpainting.pth" \
    "${weights}/facelib/detection_Resnet50_Final.pth" \
    "${weights}/facelib/parsing_parsenet.pth" \
    "${venv}/bin/python3"; do
    if [ ! -e "$required" ]; then
      printf 'incomplete CodeFormer runtime; missing: %s\n' "$required" >&2
      printf 'run bash tools/codeformer/setup_codeformer_runtime.sh on this target platform and python3 tools/codeformer/download_codeformer_models.py\n' >&2
      exit 1
    fi
  done

  mkdir -p "${install_root}/codeformer"
  cp -a "$source" "${install_root}/codeformer/CodeFormer"
  rm -rf "${install_root}/codeformer/CodeFormer/weights"
  cp -a "$weights" "${install_root}/codeformer/CodeFormer/weights"
  cp -a "$venv" "${install_root}/codeformer/venv"

  local staged_venv="${install_root}/codeformer/venv"
  if [ "$(uname -s)" = "Darwin" ]; then
    # A venv normally contains a symlink to the build machine's Python.
    # Bundle the matching framework runtime so the AI package also works on
    # Macs without Xcode, Homebrew or a source checkout.
    local python_base
    python_base="$("${venv}/bin/python3" -c 'import sys; print(sys.base_prefix)' 2>/dev/null || true)"
    if [ -z "$python_base" ] || [ ! -x "${python_base}/bin/python3" ] \
      || [ ! -f "${python_base}/Python3" ] || [ ! -d "${python_base}/lib" ]; then
      printf 'CodeFormer base Python runtime is not packageable: %s\n' "${python_base:-unknown}" >&2
      exit 1
    fi
    cp -a "$python_base" "${install_root}/codeformer/python"

    rm -f "${staged_venv}/bin/python3"
    ln -s ../../python/bin/python3 "${staged_venv}/bin/python3"
    ln -sfn python3 "${staged_venv}/bin/python"
    local python_abi
    python_abi="$("${venv}/bin/python3" -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
    if [ -e "${install_root}/codeformer/python/bin/python${python_abi}" ]; then
      ln -sfn python3 "${staged_venv}/bin/python${python_abi}"
    fi
    local python_version
    python_version="$("${venv}/bin/python3" -c 'import platform; print(platform.python_version())')"
    printf 'home = %s\ninclude-system-site-packages = false\nversion = %s\n' \
      "${INSTALL_PREFIX}/codeformer/python/bin" "$python_version" \
      > "${staged_venv}/pyvenv.cfg"
  fi

  # Virtual environments contain absolute references to the checkout in
  # entry-point shebangs, activation scripts, .pth files and editable-install
  # metadata.  Rewrite those references to the final installation prefix.
  local final_root="${INSTALL_PREFIX}/codeformer"
  local source_platform_venv="${source_root}/venv/${platform_venv}"
  local text_file
  while IFS= read -r -d '' text_file; do
    if LC_ALL=C grep -Iq . "$text_file" \
      && LC_ALL=C grep -qF "$source_root" "$text_file"; then
      if [ "$(uname -s)" = "Darwin" ]; then
        sed -i '' "s|${source_platform_venv}|${final_root}/venv|g" "$text_file"
        sed -i '' "s|${source_root}|${final_root}|g" "$text_file"
      else
        sed -i "s|${source_platform_venv}|${final_root}/venv|g" "$text_file"
        sed -i "s|${source_root}|${final_root}|g" "$text_file"
      fi
    fi
  done < <(
    find "$staged_venv/bin" -type f -print0
    find "$staged_venv/lib" -type f \( -name '*.pth' -o -name '*.egg-link' \) -print0
  )
  return 0
}

copy_native_codeformer_assets() {
  local install_root="$1"
  if [ "$(uname -s)" != "Darwin" ]; then
    return 0
  fi

  local executable="${TOOLS_ROOT}/mac/coreml-codeformer"
  local inpainting_model="${MODELS_ROOT}/codeformer/coreml/codeformer-inpainting.mlmodelc"
  if [ ! -x "$executable" ] || [ ! -d "$inpainting_model" ]; then
    log "warning: native Core ML CodeFormer runtime is unavailable; Python fallback will be used"
    return 0
  fi
  cp -a "$executable" "${install_root}/bin/coreml-codeformer"
  chmod 0755 "${install_root}/bin/coreml-codeformer"
  mkdir -p "${install_root}/models/codeformer"
  cp -a "$inpainting_model" "${install_root}/models/codeformer/"
}

stage_ai_runtime_assets() {
  local install_root="$1"

  mkdir -p \
    "$install_root/bin" \
    "$install_root/libexec" \
    "$install_root/models"

  copy_realesrgan_runtime "$install_root"
  copy_native_codeformer_assets "$install_root"
  copy_codeformer_assets "$install_root"
  copy_if_exists "${TOOLS_ROOT}/codeformer/codeformer_runner.py" "$install_root/libexec/"
  copy_if_exists "${TOOLS_ROOT}/codeformer/CODEFORMER.md" "$install_root/"
  copy_if_exists "${TOOLS_ROOT}/codeformer/codeformer-constraints.txt" "$install_root/CODEFORMER-CONSTRAINTS.txt"
  copy_if_exists "${TOOLS_ROOT}/codeformer/setup_codeformer_runtime.sh" "$install_root/setup-codeformer-runtime.sh"
  copy_if_exists "${TOOLS_ROOT}/codeformer/prepare_codeformer_source.py" "$install_root/"
}

stage_ai_runtime_tree() {
  local stage_root="$1"
  local version="${2:-${DEFAULT_VERSION}}"
  local install_root="${stage_root}${INSTALL_PREFIX}"

  stage_ai_runtime_assets "$install_root"
  printf '%s\n' "$version" > "${install_root}/AI-PACKAGE-VERSION"
}

verify_macos_ai_payload() {
  local install_root="$1"
  local required
  local missing=0

  for required in \
    "bin/realesrgan-ncnn-vulkan" \
    "bin/coreml-realesrgan" \
    "bin/red-eye-correct" \
    "bin/coreml-codeformer" \
    "models/realesrgan" \
    "models/coreml" \
    "models/restormer" \
    "models/codeformer/codeformer-inpainting.mlmodelc" \
    "codeformer/CodeFormer/inference_codeformer.py" \
    "codeformer/CodeFormer/weights/CodeFormer/codeformer.pth" \
    "codeformer/CodeFormer/weights/CodeFormer/codeformer_inpainting.pth" \
    "codeformer/python/bin/python3" \
    "codeformer/venv/bin/python3" \
    "libexec/codeformer_runner.py"; do
    if [ ! -e "${install_root}/${required}" ]; then
      log "error: incomplete macOS AI package payload; missing ${required}" >&2
      missing=1
    fi
  done

  if [ "$missing" -ne 0 ]; then
    log "prepare all AI runtimes and models before building the AI package" >&2
    return 1
  fi
}

verify_linux_ai_payload() {
  local install_root="$1"
  local required
  local missing=0

  for required in \
    "bin/realesrgan-ncnn-vulkan" \
    "models/realesrgan/realesrgan-x4plus.param" \
    "models/realesrgan/realesrgan-x4plus.bin" \
    "models/realesrgan/realesr-animevideov3-x2.param" \
    "models/realesrgan/realesr-animevideov3-x2.bin" \
    "models/realesrgan/realesr-animevideov3-x3.param" \
    "models/realesrgan/realesr-animevideov3-x3.bin" \
    "models/realesrgan/realesr-animevideov3-x4.param" \
    "models/realesrgan/realesr-animevideov3-x4.bin" \
    "codeformer/CodeFormer/inference_codeformer.py" \
    "codeformer/CodeFormer/weights/CodeFormer/codeformer.pth" \
    "codeformer/CodeFormer/weights/CodeFormer/codeformer_inpainting.pth" \
    "codeformer/venv/bin/python3" \
    "libexec/codeformer_runner.py"; do
    if [ ! -e "${install_root}/${required}" ]; then
      log "error: incomplete Linux AI package payload; missing ${required}" >&2
      missing=1
    fi
  done

  if [ "$missing" -ne 0 ]; then
    log "prepare the Linux AI runtimes and models before building the AI package" >&2
    return 1
  fi

  if ! "${install_root}/codeformer/venv/bin/python3" -c \
    'import cv2, torch' >/dev/null 2>&1; then
    log "error: packaged CodeFormer Python runtime cannot import cv2 and torch" >&2
    log "create tools/codeformer/venv/linux on the target Ubuntu architecture before packaging" >&2
    return 1
  fi
}

stage_runtime_tree() {
  local stage_root="$1"
  local include_ai="${2:-1}"
  local install_root="${stage_root}${INSTALL_PREFIX}"

  mkdir -p \
    "$install_root/sbin" \
    "$install_root/conf" \
    "$install_root/uploads" \
    "$install_root/lib" \
    "$install_root/libexec" \
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

  if is_macos_host && [ -d "${WEBCOOL_ROOT}/webcool.app" ]; then
    mkdir -p "${stage_root}/Applications"
    cp -a "${WEBCOOL_ROOT}/webcool.app" "${stage_root}/Applications/"
  fi

  if [ ! -f "${WEBCOOL_ROOT}/webcool.cf" ]; then
    printf 'webcool config not found: %s\n' "${WEBCOOL_ROOT}/webcool.cf" >&2
    exit 1
  fi
  cp -a "${WEBCOOL_ROOT}/webcool.cf" "${install_root}/conf/webcool.cf"

  copy_acl_runtime_libs "$install_root/lib"
  copy_sqlite_runtime_lib "$install_root/lib"
  copy_ffmpeg_runtime_bin "$install_root/bin"
  if [ "$include_ai" -eq 1 ]; then
    stage_ai_runtime_assets "$install_root"
  fi

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

  # Remove physical Finder files and removable quarantine/resource-fork xattrs.
  # macOS may retain protected provenance metadata; pkgbuild represents that as
  # AppleDouble records which Installer consumes as metadata, not visible files.
  find "$install_root" \( -name '.DS_Store' -o -name '._*' \) -delete 2>/dev/null || true
  if command -v xattr >/dev/null 2>&1; then
    xattr -cr "$install_root"
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

  local bundled_python="${stage_root}${INSTALL_PREFIX}/codeformer/python"
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
  local i
  local file_count="${#files[@]}"
  for ((i = 0; i < file_count; ++i)); do
    f="${files[$i]}"
    depth="$(printf '%s' "$f" | tr -cd '/' | wc -c | tr -d ' ')"
    sorted_files+=("${depth}	${f}")
  done
  if [ "${#sorted_files[@]}" -gt 0 ]; then
    IFS=$'\n'
    sorted_files=($(printf '%s\n' "${sorted_files[@]}" | sort -t $'\t' -k1,1nr | cut -f2-))
    unset IFS
  fi

  local signed_file_count="${#sorted_files[@]}"
  for ((i = 0; i < signed_file_count; ++i)); do
    f="${sorted_files[$i]}"
    case "$f" in
      "${bundled_python}/bin/python"*)
        codesign --force --options runtime --timestamp \
          --entitlements "${PACKAGE_ROOT}/macos-python-entitlements.plist" \
          --sign "$app_identity" "$f"
        ;;
      *)
        codesign --force --options runtime --timestamp \
          --sign "$app_identity" "$f"
        ;;
    esac
  done

  local bundles=()
  local bundle_path
  while IFS= read -r -d '' bundle_path; do
    bundles+=("$bundle_path")
  done < <(find "$stage_root" -type d \( -name '*.app' -o -name '*.framework' \) -print0)
  if [ -f "${bundled_python}/Resources/Info.plist" ]; then
    bundles+=("$bundled_python")
  fi

  local sorted_bundles=()
  local bundle_count="${#bundles[@]}"
  for ((i = 0; i < bundle_count; ++i)); do
    bundle_path="${bundles[$i]}"
    depth="$(printf '%s' "$bundle_path" | tr -cd '/' | wc -c | tr -d ' ')"
    sorted_bundles+=("${depth}	${bundle_path}")
  done
  if [ "${#sorted_bundles[@]}" -gt 0 ]; then
    IFS=$'\n'
    sorted_bundles=($(printf '%s\n' "${sorted_bundles[@]}" | sort -t $'\t' -k1,1nr | cut -f2-))
    unset IFS
    bundle_count="${#sorted_bundles[@]}"
    for ((i = 0; i < bundle_count; ++i)); do
      bundle_path="${sorted_bundles[$i]}"
      if [ "$bundle_path" = "${bundled_python}/Resources/Python.app" ]; then
        codesign --force --options runtime --timestamp \
          --entitlements "${PACKAGE_ROOT}/macos-python-entitlements.plist" \
          --sign "$app_identity" "$bundle_path"
      else
        codesign --force --options runtime --timestamp \
          --sign "$app_identity" "$bundle_path"
      fi
    done
  fi

  log "signed ${signed_file_count} Mach-O file(s) and ${bundle_count} bundle(s)"
}

build_macos_pkg() {
  local stage_root="$1"
  local identifier="$2"
  local version="$3"
  local out_pkg="$4"
  local installer_identity="$5"
  local component_index
  local component_plist=""
  local component_path
  local webcool_component_found=0

  require_cmd pkgbuild
  local unsigned_pkg="${out_pkg}.unsigned"

  rm -f "$unsigned_pkg" "$out_pkg"

  # Component packages are relocatable by default. If Launch Services has
  # already seen cn.webcool.control outside /Applications (for example, the
  # app bundle in a source checkout), Installer otherwise updates that copy
  # instead of installing /Applications/webcool.app.
  if [ -d "${stage_root}/Applications/webcool.app" ]; then
    require_cmd plutil
    component_plist="$(dirname "$stage_root")/webcool-components.plist"
    pkgbuild --analyze --root "$stage_root" "$component_plist"

    component_index=0
    while component_path="$(plutil -extract \
      "${component_index}.RootRelativeBundlePath" raw \
      "$component_plist" 2>/dev/null)"; do
      if [ "$component_path" = "Applications/webcool.app" ]; then
        plutil -replace "${component_index}.BundleIsRelocatable" \
          -bool NO \
          "$component_plist"
        webcool_component_found=1
        break
      fi
      component_index=$((component_index + 1))
    done

    if [ "$webcool_component_found" -ne 1 ]; then
      printf 'webcool.app was not found in pkgbuild component analysis\n' >&2
      exit 1
    fi

    log "disabled bundle relocation for /Applications/webcool.app"
  fi

  log "building macOS package payload: ${out_pkg}"
  if [ -n "$component_plist" ]; then
    pkgbuild \
      --root "$stage_root" \
      --component-plist "$component_plist" \
      --identifier "$identifier" \
      --version "$version" \
      "$unsigned_pkg"
  else
    pkgbuild \
      --root "$stage_root" \
      --identifier "$identifier" \
      --version "$version" \
      "$unsigned_pkg"
  fi

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
