#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/bin/Release"
DIST_DIR="$PROJECT_DIR/dist"
VERSION="$(sed -n 's/^project(termify VERSION \([^ ]*\).*/\1/p' \
  "$PROJECT_DIR/CMakeLists.txt")"
SYSTEM="$(uname -s)"
NATIVE_ARCH="$(uname -m)"
source "$PROJECT_DIR/scripts/platform.sh"
ARCH="$(termify_canonical_arch "$NATIVE_ARCH")"
case "$SYSTEM:$ARCH" in
Linux:x86_64 | Linux:arm64) PLATFORM="linux" ;;
Darwin:x86_64 | Darwin:arm64) PLATFORM="macos" ;;
*) echo "Release packaging is unsupported on $SYSTEM $NATIVE_ARCH." >&2; exit 1 ;;
esac
PACKAGE_NAME="termify-v${VERSION}-${PLATFORM}-${ARCH}"
STAGE_DIR="$DIST_DIR/$PACKAGE_NAME"
ARCHIVE="$DIST_DIR/$PACKAGE_NAME.tar.gz"
source "$PROJECT_DIR/scripts/yt-dlp.sh"

if [[ -z "$VERSION" ]]; then
  echo "Could not read the Termify version from CMakeLists.txt" >&2
  exit 1
fi

YTDLP_CACHE="$(termify_ensure_ytdlp "$PROJECT_DIR")"

cmake -E remove_directory "$BUILD_DIR"
cmake -E remove_directory "$STAGE_DIR"
cmake -E make_directory "$STAGE_DIR"

CMAKE_ARGUMENTS=(
  -S "$PROJECT_DIR"
  -B "$BUILD_DIR"
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_TESTING=ON
)
if [[ "$SYSTEM" == "Darwin" ]] && command -v brew >/dev/null 2>&1; then
  CMAKE_ARGUMENTS+=("-DCMAKE_PREFIX_PATH=$(brew --prefix)")
fi
cmake "${CMAKE_ARGUMENTS[@]}"
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure

install_file() {
  local mode="$1"
  local source="$2"
  local destination="$3"
  mkdir -p "$(dirname "$destination")"
  install -m "$mode" "$source" "$destination"
}

install_file 755 "$BUILD_DIR/termify" "$STAGE_DIR/termify"
if command -v strip >/dev/null 2>&1; then
  if [[ "$SYSTEM" == "Darwin" ]]; then
    strip -x "$STAGE_DIR/termify"
  else
    strip --strip-unneeded "$STAGE_DIR/termify"
  fi
fi
install_file 755 "$YTDLP_CACHE" "$STAGE_DIR/yt-dlp"
install_file 755 "$PROJECT_DIR/scripts/install-release.sh" \
  "$STAGE_DIR/install.sh"
install_file 644 "$PROJECT_DIR/README.md" "$STAGE_DIR/README.md"
install_file 644 "$PROJECT_DIR/LICENSE" "$STAGE_DIR/LICENSE"
install_file 644 "$PROJECT_DIR/assets/termify.png" \
  "$STAGE_DIR/assets/termify.png"
install_file 644 "$PROJECT_DIR/ROADMAP.md" "$STAGE_DIR/ROADMAP.md"
install_file 644 "$PROJECT_DIR/docs/TUI.md" "$STAGE_DIR/docs/TUI.md"
install_file 644 "$PROJECT_DIR/docs/PLATFORM.md" \
  "$STAGE_DIR/docs/PLATFORM.md"
install_file 644 "$PROJECT_DIR/docs/RELEASE.md" "$STAGE_DIR/docs/RELEASE.md"

if tar --version 2>/dev/null | grep -q 'GNU tar'; then
  tar --sort=name --owner=0 --group=0 --numeric-owner \
    -czf "$ARCHIVE" -C "$DIST_DIR" "$PACKAGE_NAME"
else
  tar -czf "$ARCHIVE" -C "$DIST_DIR" "$PACKAGE_NAME"
fi
termify_write_sha256 "$ARCHIVE" "$ARCHIVE.sha256"

"$STAGE_DIR/termify" --version
"$STAGE_DIR/yt-dlp" --version
echo "Created $ARCHIVE"
echo "Created $ARCHIVE.sha256"
