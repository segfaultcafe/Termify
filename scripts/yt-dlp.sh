#!/usr/bin/env bash

# Source builds and release packages must use the same verified standalone
# downloader. Do not fall back to the obsolete binary in ext/.
TERMIFY_YTDLP_VERSION="2026.10.26"
TERMIFY_YTDLP_LINUX_X86_64_SHA256="767ba9dce7052aa196f8b12652f433d76de128ba168966fa84b6394d5e6c6795"
TERMIFY_YTDLP_MACOS_SHA256="0f922c15ccc0b83eefa1c8bc9104e434318c4bf16e408e1ee93b7745a6efe0aa"

termify_sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  else
    echo "A SHA-256 tool (sha256sum or shasum) is required." >&2
    return 1
  fi
}

termify_verify_sha256() {
  local expected="$1"
  local file="$2"
  [[ -f "$file" && "$(termify_sha256_file "$file")" == "$expected" ]]
}

termify_write_sha256() {
  local file="$1"
  local output="$2"
  printf '%s  %s\n' "$(termify_sha256_file "$file")" \
    "$(basename "$file")" >"$output"
}

termify_ensure_ytdlp() (
  set -euo pipefail
  local project_dir="$1"
  local system arch platform asset checksum
  system="$(uname -s)"
  arch="$(uname -m)"

  case "$system:$arch" in
  Linux:x86_64)
    platform="linux"
    asset="yt-dlp_linux"
    checksum="$TERMIFY_YTDLP_LINUX_X86_64_SHA256"
    ;;
  Darwin:x86_64 | Darwin:arm64)
    platform="macos"
    asset="yt-dlp_macos"
    checksum="$TERMIFY_YTDLP_MACOS_SHA256"
    ;;
  *)
    echo "No bundled yt-dlp is available for $system $arch." >&2
    exit 1
    ;;
  esac

  local cache="$project_dir/.cache/release/yt-dlp-${TERMIFY_YTDLP_VERSION}-${platform}-${arch}"
  local url="https://github.com/yt-dlp/yt-dlp/releases/download/${TERMIFY_YTDLP_VERSION}/${asset}"
  if ! termify_verify_sha256 "$checksum" "$cache"; then
    mkdir -p "$(dirname "$cache")"
    local partial
    partial="$(mktemp "${cache}.part.XXXXXX")"
    trap 'rm -f -- "$partial"' EXIT
    echo "Fetching standalone yt-dlp ${TERMIFY_YTDLP_VERSION}..." >&2
    curl --proto '=https' --tlsv1.2 -fL --retry 2 "$url" -o "$partial"
    if ! termify_verify_sha256 "$checksum" "$partial"; then
      echo "yt-dlp checksum verification failed." >&2
      exit 1
    fi
    chmod 0755 "$partial"
    mv -f -- "$partial" "$cache"
  fi
  printf '%s\n' "$cache"
)
