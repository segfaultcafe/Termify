#!/usr/bin/env bash
set -euo pipefail

PACKAGE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if (( $# > 1 )); then
  echo "Usage: ./install.sh [PREFIX]" >&2
  exit 2
fi

INSTALL_PREFIX="${1:-${PREFIX:-$HOME/.local}}"

if [[ ! -x "$PACKAGE_DIR/termify" || ! -x "$PACKAGE_DIR/yt-dlp" ]]; then
  echo "The release archive is incomplete: termify or yt-dlp is missing." >&2
  exit 1
fi

install -Dm755 "$PACKAGE_DIR/termify" "$INSTALL_PREFIX/bin/termify"
install -Dm755 "$PACKAGE_DIR/yt-dlp" \
  "$INSTALL_PREFIX/lib/termify/yt-dlp"

echo "Installed Termify to $INSTALL_PREFIX/bin/termify"
echo "Installed the embedded yt-dlp to $INSTALL_PREFIX/lib/termify/yt-dlp"
echo "Add $INSTALL_PREFIX/bin to PATH if it is not already present."
echo "No Python or separate yt-dlp installation is required."
