#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/bin/AppImage"
DIST_DIR="$PROJECT_DIR/dist"
VERSION="$(sed -n 's/^project(termify VERSION \([^ ]*\).*/\1/p' \
  "$PROJECT_DIR/CMakeLists.txt")"
NATIVE_ARCH="$(uname -m)"
source "$PROJECT_DIR/scripts/platform.sh"
ARCH="$(termify_canonical_arch "$NATIVE_ARCH")"
APPIMAGE_ARCH="$(termify_appimage_arch "$ARCH")"
APPDIR="$DIST_DIR/termify.AppDir"
APPIMAGE="$DIST_DIR/termify-v${VERSION}-linux-${ARCH}.AppImage"
source "$PROJECT_DIR/scripts/yt-dlp.sh"
source "$PROJECT_DIR/scripts/appimage-tools.sh"

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "AppImage packaging requires Linux, not $(uname -s)." >&2
  exit 1
fi
if [[ -z "$VERSION" ]]; then
  echo "Could not read the Termify version from CMakeLists.txt" >&2
  exit 1
fi

YTDLP_CACHE="$(termify_ensure_ytdlp "$PROJECT_DIR")"
CURL_EXECUTABLE="$(command -v curl || true)"
if [[ -z "$CURL_EXECUTABLE" ]]; then
  echo "curl is required to package the AppImage update checker." >&2
  exit 1
fi
LINUXDEPLOY="$(termify_ensure_linuxdeploy "$PROJECT_DIR" "$APPIMAGE_ARCH")"
APPIMAGETOOL="$(termify_ensure_appimagetool "$PROJECT_DIR" "$APPIMAGE_ARCH")"
APPIMAGE_RUNTIME="$(termify_ensure_appimage_runtime "$PROJECT_DIR" "$APPIMAGE_ARCH")"

cmake -E remove_directory "$BUILD_DIR"
cmake -E remove_directory "$APPDIR"
rm -f -- "$APPIMAGE" "$APPIMAGE.sha256"

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DBUILD_TESTING=ON
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --strip

install -Dm755 "$YTDLP_CACHE" "$APPDIR/usr/lib/termify/yt-dlp"
install -Dm755 "$CURL_EXECUTABLE" "$APPDIR/usr/bin/curl"
install -Dm644 "$PROJECT_DIR/LICENSE" \
  "$APPDIR/usr/share/doc/termify/LICENSE"
install -Dm644 "$PROJECT_DIR/README.md" \
  "$APPDIR/usr/share/doc/termify/README.md"
if [[ -f /usr/share/doc/curl/copyright ]]; then
  install -Dm644 /usr/share/doc/curl/copyright \
    "$APPDIR/usr/share/doc/curl/copyright"
fi

APPIMAGE_EXTRACT_AND_RUN=1 "$LINUXDEPLOY" \
  --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/termify" \
  --executable "$APPDIR/usr/bin/curl" \
  --desktop-file "$PROJECT_DIR/packaging/appimage/termify.desktop" \
  --icon-file "$PROJECT_DIR/packaging/appimage/termify.svg"

# linuxdeploy excludes several host-integration libraries. Debian's FFmpeg
# build links against the graphics/font stack even for audio-only decoding,
# so a minimal/headless distribution does not necessarily provide them.
bundle_excluded_library() {
  local library="$1"
  local source
  source="$(ldconfig -p | awk -v library="$library" \
    '$1 == library && !found { path = $NF; found = 1 }
     END { if (found) print path }')"
  if [[ -z "$source" || ! -f "$source" ]]; then
    echo "Could not locate $library for the AppImage." >&2
    exit 1
  fi
  source="$(readlink -f "$source")"
  install -Dm755 "$source" "$APPDIR/usr/lib/$library"

  if command -v dpkg-query >/dev/null 2>&1; then
    local package copyright
    if package="$(dpkg-query --search "$source" 2>/dev/null | \
      awk -F': ' 'NR == 1 { print $1 }')" && [[ -n "$package" ]]; then
      package="${package%%:*}"
      copyright="/usr/share/doc/$package/copyright"
      if [[ -f "$copyright" ]]; then
        install -Dm644 "$copyright" \
          "$APPDIR/usr/share/doc/$package/copyright"
      fi
    fi
  fi
}

EXCLUDED_RUNTIME_LIBRARIES=(
  libasound.so.2
  libX11.so.6
  libX11-xcb.so.1
  libdrm.so.2
  libexpat.so.1
  libfontconfig.so.1
  libfreetype.so.6
  libfribidi.so.0
  libharfbuzz.so.0
  libxcb.so.1
  libxcb-dri3.so.0
)
for library in "${EXCLUDED_RUNTIME_LIBRARIES[@]}"; do
  bundle_excluded_library "$library"
done

# linuxdeploy assembles the dependency closure. Termify's executable discovery
# finds the private updater-managed downloader first and this bundled copy as
# its immutable fallback.
install -Dm755 "$PROJECT_DIR/packaging/appimage/AppRun" "$APPDIR/AppRun"
ln -sfn usr/share/applications/termify.desktop "$APPDIR/termify.desktop"
ln -sfn usr/share/icons/hicolor/scalable/apps/termify.svg \
  "$APPDIR/termify.svg"
ln -sfn termify.svg "$APPDIR/.DirIcon"

ARCH="$APPIMAGE_ARCH" VERSION="$VERSION" APPIMAGE_EXTRACT_AND_RUN=1 \
  "$APPIMAGETOOL" --runtime-file "$APPIMAGE_RUNTIME" \
  "$APPDIR" "$APPIMAGE"
chmod 0755 "$APPIMAGE"
termify_write_sha256 "$APPIMAGE" "$APPIMAGE.sha256"

APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGE" --version
echo "Created $APPIMAGE"
echo "Created $APPIMAGE.sha256"
