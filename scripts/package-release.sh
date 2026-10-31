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
YTDLP_VERSION="2026.10.26"
YTDLP_SHA256="767ba9dce7052aa196f8b12652f433d76de128ba168966fa84b6394d5e6c6795"
YTDLP_CACHE="$PROJECT_DIR/.cache/release/yt-dlp-${YTDLP_VERSION}-linux-${ARCH}"
YTDLP_URL="https://github.com/yt-dlp/yt-dlp/releases/download/${YTDLP_VERSION}/yt-dlp_linux"

if [[ -z "$VERSION" ]]; then
  echo "Could not read the Termify version from CMakeLists.txt" >&2
  exit 1
fi

if [[ "$ARCH" != "x86_64" ]]; then
  echo "The v${VERSION} prebuilt package currently supports x86_64 only." >&2
  exit 1
fi

cmake -E remove_directory "$BUILD_DIR"
cmake -E remove_directory "$STAGE_DIR"
cmake -E make_directory "$STAGE_DIR"

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure

cmake -E make_directory "$(dirname "$YTDLP_CACHE")"
if ! printf '%s  %s\n' "$YTDLP_SHA256" "$YTDLP_CACHE" \
    | sha256sum --check --status 2>/dev/null; then
  YTDLP_PART="${YTDLP_CACHE}.part"
  cmake -E rm -f "$YTDLP_PART"
  curl --proto '=https' --tlsv1.2 -fL "$YTDLP_URL" -o "$YTDLP_PART"
  printf '%s  %s\n' "$YTDLP_SHA256" "$YTDLP_PART" | sha256sum -c -
  chmod 0755 "$YTDLP_PART"
  mv "$YTDLP_PART" "$YTDLP_CACHE"
fi
printf '%s  %s\n' "$YTDLP_SHA256" "$YTDLP_CACHE" | sha256sum -c -

install -Dm755 "$BUILD_DIR/termify" "$STAGE_DIR/termify"
if command -v strip >/dev/null 2>&1; then
  strip --strip-unneeded "$STAGE_DIR/termify"
fi
install -Dm755 "$YTDLP_CACHE" "$STAGE_DIR/yt-dlp"
install -Dm755 "$PROJECT_DIR/scripts/install-release.sh" \
  "$STAGE_DIR/install.sh"
install -Dm644 "$PROJECT_DIR/README.md" "$STAGE_DIR/README.md"
install -Dm644 "$PROJECT_DIR/LICENSE" "$STAGE_DIR/LICENSE"

tar --sort=name --owner=0 --group=0 --numeric-owner \
  -czf "$ARCHIVE" -C "$DIST_DIR" "$PACKAGE_NAME"
sha256sum "$ARCHIVE" > "$ARCHIVE.sha256"

"$STAGE_DIR/termify" --version
"$STAGE_DIR/yt-dlp" --version
echo "Created $ARCHIVE"
echo "Created $ARCHIVE.sha256"
