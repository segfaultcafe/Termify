#!/usr/bin/env bash
set -euo pipefail

PACKAGE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if (( $# > 1 )); then
  echo "Usage: ./install.sh [PREFIX]" >&2
  exit 2
fi

INSTALL_PREFIX="${1:-${PREFIX:-$HOME/.local}}"

install_executable() {
  local source="$1"
  local destination="$2"
  mkdir -p "$(dirname "$destination")" &&
    install -m 755 "$source" "$destination"
}

if [[ ! -x "$PACKAGE_DIR/termify" || ! -x "$PACKAGE_DIR/yt-dlp" ]]; then
  echo "The release archive is incomplete: termify or yt-dlp is missing." >&2
  exit 1
fi

install_executable "$PACKAGE_DIR/termify" "$INSTALL_PREFIX/bin/termify"
YTDLP_TARGET="$INSTALL_PREFIX/lib/termify/yt-dlp"
if ! install_executable "$PACKAGE_DIR/yt-dlp" "$YTDLP_TARGET" 2>/dev/null; then
  YTDLP_TARGET="$INSTALL_PREFIX/bin/yt-dlp"
  echo "Could not write to $INSTALL_PREFIX/lib; using $YTDLP_TARGET instead." >&2
  install_executable "$PACKAGE_DIR/yt-dlp" "$YTDLP_TARGET"
fi

echo "Installed Termify to $INSTALL_PREFIX/bin/termify"
echo "Installed the embedded yt-dlp to $YTDLP_TARGET"
echo "Add $INSTALL_PREFIX/bin to PATH if it is not already present."
echo "No Python or separate yt-dlp installation is required."
