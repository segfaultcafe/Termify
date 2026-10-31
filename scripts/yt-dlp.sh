#!/usr/bin/env bash

# Source builds and release packages must use the same verified standalone
# downloader. Do not fall back to the obsolete binary in ext/.
TERMIFY_YTDLP_VERSION="2026.10.26"
TERMIFY_YTDLP_SHA256="767ba9dce7052aa196f8b12652f433d76de128ba168966fa84b6394d5e6c6795"

termify_ensure_ytdlp() (
  set -euo pipefail
  local project_dir="$1"
  local arch
  arch="$(uname -m)"
  if [[ "$arch" != "x86_64" ]]; then
    echo "The bundled yt-dlp currently supports x86_64 Linux only." >&2
    exit 1
  fi

  local cache="$project_dir/.cache/release/yt-dlp-${TERMIFY_YTDLP_VERSION}-linux-${arch}"
  local url="https://github.com/yt-dlp/yt-dlp/releases/download/${TERMIFY_YTDLP_VERSION}/yt-dlp_linux"
  if ! printf '%s  %s\n' "$TERMIFY_YTDLP_SHA256" "$cache" \
      | sha256sum --check --status 2>/dev/null; then
    mkdir -p "$(dirname "$cache")"
    local partial
    partial="$(mktemp "${cache}.part.XXXXXX")"
    trap 'rm -f -- "$partial"' EXIT
    echo "Fetching standalone yt-dlp ${TERMIFY_YTDLP_VERSION}..." >&2
    curl --proto '=https' --tlsv1.2 -fL --retry 2 "$url" -o "$partial"
    printf '%s  %s\n' "$TERMIFY_YTDLP_SHA256" "$partial" | sha256sum -c - >&2
    chmod 0755 "$partial"
    mv -f -- "$partial" "$cache"
  fi
  printf '%s\n' "$cache"
)
