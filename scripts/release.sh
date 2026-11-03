#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(sed -n 's/^project(termify VERSION \([^ ]*\).*/\1/p' \
  "$PROJECT_DIR/CMakeLists.txt")"
ARCH="$(uname -m)"
case "$(uname -s)" in
Linux) PLATFORM="linux" ;;
Darwin) PLATFORM="macos" ;;
*) echo "Unsupported release platform: $(uname -s)" >&2; exit 1 ;;
esac
PACKAGE_DIR="$PROJECT_DIR/dist/termify-v${VERSION}-${PLATFORM}-${ARCH}"

"$PROJECT_DIR/scripts/package-release.sh"
sudo "$PACKAGE_DIR/install.sh" /usr/local
