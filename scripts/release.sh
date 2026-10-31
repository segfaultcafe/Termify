#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(sed -n 's/^project(termify VERSION \([^ ]*\).*/\1/p' \
  "$PROJECT_DIR/CMakeLists.txt")"
ARCH="$(uname -m)"
PACKAGE_DIR="$PROJECT_DIR/dist/termify-v${VERSION}-linux-${ARCH}"

"$PROJECT_DIR/scripts/package-release.sh"
sudo install -Dm755 "$PACKAGE_DIR/termify" /usr/local/bin/termify
sudo install -Dm755 "$PACKAGE_DIR/yt-dlp" \
  /usr/local/lib/termify/yt-dlp

echo "Installed Termify to /usr/local/bin/termify"
echo "Installed embedded yt-dlp to /usr/local/lib/termify/yt-dlp"
echo "No Python or separate yt-dlp installation is required."
