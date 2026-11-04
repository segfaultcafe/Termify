#!/usr/bin/env bash

# AppImage build inputs are pinned to immutable upstream releases and verified
# before use. Keep these values in sync with the official GitHub release asset
# digests when deliberately upgrading the packaging toolchain.
TERMIFY_LINUXDEPLOY_VERSION="1-alpha-20251107-1"
TERMIFY_LINUXDEPLOY_X86_64_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
TERMIFY_APPIMAGETOOL_VERSION="1.9.1"
TERMIFY_APPIMAGETOOL_X86_64_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
TERMIFY_APPIMAGE_RUNTIME_VERSION="20251108"
TERMIFY_APPIMAGE_RUNTIME_X86_64_SHA256="2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"

termify_fetch_verified_tool() (
  set -euo pipefail
  local url="$1"
  local checksum="$2"
  local destination="$3"

  if termify_verify_sha256 "$checksum" "$destination"; then
    printf '%s\n' "$destination"
    exit 0
  fi

  mkdir -p "$(dirname "$destination")"
  local partial
  partial="$(mktemp "${destination}.part.XXXXXX")"
  trap 'rm -f -- "$partial"' EXIT
  echo "Fetching $(basename "$destination")..." >&2
  curl --proto '=https' --tlsv1.2 -fL --retry 2 "$url" -o "$partial"
  if ! termify_verify_sha256 "$checksum" "$partial"; then
    echo "Checksum verification failed for $(basename "$destination")." >&2
    exit 1
  fi
  chmod 0755 "$partial"
  mv -f -- "$partial" "$destination"
  printf '%s\n' "$destination"
)

termify_ensure_linuxdeploy() {
  local project_dir="$1"
  local cache="$project_dir/.cache/appimage/linuxdeploy-${TERMIFY_LINUXDEPLOY_VERSION}-x86_64.AppImage"
  local url="https://github.com/linuxdeploy/linuxdeploy/releases/download/${TERMIFY_LINUXDEPLOY_VERSION}/linuxdeploy-x86_64.AppImage"
  termify_fetch_verified_tool "$url" \
    "$TERMIFY_LINUXDEPLOY_X86_64_SHA256" "$cache"
}

termify_ensure_appimagetool() {
  local project_dir="$1"
  local cache="$project_dir/.cache/appimage/appimagetool-${TERMIFY_APPIMAGETOOL_VERSION}-x86_64.AppImage"
  local url="https://github.com/AppImage/appimagetool/releases/download/${TERMIFY_APPIMAGETOOL_VERSION}/appimagetool-x86_64.AppImage"
  termify_fetch_verified_tool "$url" \
    "$TERMIFY_APPIMAGETOOL_X86_64_SHA256" "$cache"
}

termify_ensure_appimage_runtime() {
  local project_dir="$1"
  local cache="$project_dir/.cache/appimage/runtime-${TERMIFY_APPIMAGE_RUNTIME_VERSION}-x86_64"
  local url="https://github.com/AppImage/type2-runtime/releases/download/${TERMIFY_APPIMAGE_RUNTIME_VERSION}/runtime-x86_64"
  termify_fetch_verified_tool "$url" \
    "$TERMIFY_APPIMAGE_RUNTIME_X86_64_SHA256" "$cache"
}
