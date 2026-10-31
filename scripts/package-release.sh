#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/bin/Release"
DIST_DIR="$PROJECT_DIR/dist"
VERSION="$(sed -n 's/^project(termify VERSION \([^ ]*\).*/\1/p' \
  "$PROJECT_DIR/CMakeLists.txt")"
ARCH="$(uname -m)"
PACKAGE_NAME="termify-v${VERSION}-linux-${ARCH}"
STAGE_DIR="$DIST_DIR/$PACKAGE_NAME"
ARCHIVE="$DIST_DIR/$PACKAGE_NAME.tar.gz"
source "$PROJECT_DIR/scripts/yt-dlp.sh"

if [[ -z "$VERSION" ]]; then
  echo "Could not read the Termify version from CMakeLists.txt" >&2
  exit 1
fi

if [[ "$ARCH" != "x86_64" ]]; then
  echo "The v${VERSION} prebuilt package currently supports x86_64 only." >&2
  exit 1
fi

YTDLP_CACHE="$(termify_ensure_ytdlp "$PROJECT_DIR")"

cmake -E remove_directory "$BUILD_DIR"
cmake -E remove_directory "$STAGE_DIR"
cmake -E make_directory "$STAGE_DIR"

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure

install -Dm755 "$BUILD_DIR/termify" "$STAGE_DIR/termify"
if command -v strip >/dev/null 2>&1; then
  strip --strip-unneeded "$STAGE_DIR/termify"
fi
install -Dm755 "$YTDLP_CACHE" "$STAGE_DIR/yt-dlp"
install -Dm755 "$PROJECT_DIR/scripts/install-release.sh" \
  "$STAGE_DIR/install.sh"
install -Dm644 "$PROJECT_DIR/README.md" "$STAGE_DIR/README.md"
install -Dm644 "$PROJECT_DIR/LICENSE" "$STAGE_DIR/LICENSE"
install -Dm644 "$PROJECT_DIR/assets/termify.png" "$STAGE_DIR/assets/termify.png"
install -Dm644 "$PROJECT_DIR/ROADMAP.md" "$STAGE_DIR/ROADMAP.md"
install -Dm644 "$PROJECT_DIR/docs/TUI.md" "$STAGE_DIR/docs/TUI.md"

tar --sort=name --owner=0 --group=0 --numeric-owner \
  -czf "$ARCHIVE" -C "$DIST_DIR" "$PACKAGE_NAME"
(cd "$DIST_DIR" && sha256sum "$PACKAGE_NAME.tar.gz" > "$PACKAGE_NAME.tar.gz.sha256")

"$STAGE_DIR/termify" --version
"$STAGE_DIR/yt-dlp" --version
echo "Created $ARCHIVE"
echo "Created $ARCHIVE.sha256"
