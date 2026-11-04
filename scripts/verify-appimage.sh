#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$PROJECT_DIR/scripts/yt-dlp.sh"

if (( $# > 1 )); then
  echo "Usage: verify-appimage.sh [ARCHITECTURE]" >&2
  exit 2
fi

ARCHITECTURE="$(termify_canonical_arch "${1:-x86_64}")"
VERSION="$(sed -n 's/^project(termify VERSION \([^ ]*\).*/\1/p' \
  "$PROJECT_DIR/CMakeLists.txt")"
NAME="termify-v${VERSION}-linux-${ARCHITECTURE}.AppImage"
APPIMAGE="$PROJECT_DIR/dist/$NAME"
CHECKSUM="$APPIMAGE.sha256"

if [[ "$(uname -s)" != "Linux" ]] || \
  ! termify_arch_matches_host "$ARCHITECTURE"; then
  echo "Cannot verify a Linux $ARCHITECTURE AppImage on $(uname -s) $(uname -m)." >&2
  exit 1
fi
if [[ -z "$VERSION" || ! -x "$APPIMAGE" || ! -f "$CHECKSUM" ]]; then
  echo "The expected executable $NAME and checksum were not produced." >&2
  exit 1
fi

read -r EXPECTED_DIGEST CHECKSUM_NAME CHECKSUM_EXTRA <"$CHECKSUM"
if [[ -n "${CHECKSUM_EXTRA:-}" || "$CHECKSUM_NAME" != "$NAME" ]]; then
  echo "The checksum file does not name exactly $NAME." >&2
  exit 1
fi
if ! termify_verify_sha256 "$EXPECTED_DIGEST" "$APPIMAGE"; then
  echo "Checksum verification failed for $APPIMAGE." >&2
  exit 1
fi

SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/termify-appimage.XXXXXX")"
trap 'rm -rf -- "$SCRATCH"' EXIT
(
  cd "$SCRATCH"
  "$APPIMAGE" --appimage-extract >/dev/null
)
APPDIR="$SCRATCH/squashfs-root"

for file in AppRun termify.desktop termify.svg usr/bin/termify \
  usr/bin/curl usr/lib/termify/yt-dlp; do
  if [[ ! -e "$APPDIR/$file" ]]; then
    echo "$file is missing from $NAME." >&2
    exit 1
  fi
done
if ! grep -qx 'Terminal=true' "$APPDIR/termify.desktop"; then
  echo "The AppImage desktop entry must launch inside a terminal." >&2
  exit 1
fi
if grep -q 'TERMIFY_YTDLP' "$APPDIR/AppRun"; then
  echo "AppRun overrides Termify's independently updated yt-dlp." >&2
  exit 1
fi
if ! grep -q 'usr/bin.*PATH' "$APPDIR/AppRun"; then
  echo "AppRun does not expose the bundled curl executable." >&2
  exit 1
fi

for library in libavformat libavcodec libavutil libswresample libsqlite3 \
  libasound; do
  if ! find "$APPDIR/usr/lib" -type f -name "${library}.so*" -print -quit |
    grep -q .; then
    echo "$library is not bundled in $NAME." >&2
    exit 1
  fi
done
for library in libX11.so.6 libX11-xcb.so.1 libdrm.so.2 libexpat.so.1 \
  libfontconfig.so.1 libfreetype.so.6 libfribidi.so.0 libharfbuzz.so.0 \
  libxcb.so.1 libxcb-dri3.so.0; do
  if [[ ! -f "$APPDIR/usr/lib/$library" ]]; then
    echo "$library is not bundled in $NAME." >&2
    exit 1
  fi
done
if LD_LIBRARY_PATH="$APPDIR/usr/lib" ldd "$APPDIR/usr/bin/termify" |
  grep -q 'not found'; then
  echo "The extracted Termify executable has unresolved libraries." >&2
  exit 1
fi
if LD_LIBRARY_PATH="$APPDIR/usr/lib" ldd "$APPDIR/usr/bin/curl" |
  grep -q 'not found'; then
  echo "The extracted curl executable has unresolved libraries." >&2
  exit 1
fi

if [[ "$(APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGE" --version)" != \
  "termify $VERSION" ]]; then
  echo "The AppImage reports the wrong Termify version." >&2
  exit 1
fi
if [[ "$("$APPDIR/usr/lib/termify/yt-dlp" --version)" != \
  "$TERMIFY_YTDLP_VERSION" ]]; then
  echo "The AppImage contains the wrong yt-dlp version." >&2
  exit 1
fi
if ! LD_LIBRARY_PATH="$APPDIR/usr/lib" \
  "$APPDIR/usr/bin/curl" --version >/dev/null; then
  echo "The AppImage contains a broken curl executable." >&2
  exit 1
fi

APPIMAGE_EXTRACT_AND_RUN=1 sh "$PROJECT_DIR/scripts/smoke-test.sh" "$APPIMAGE"
echo "Verified $NAME, its dependency bundle, and clean startup."
