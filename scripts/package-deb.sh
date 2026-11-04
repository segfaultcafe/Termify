#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/bin/Release-deb"
DIST_DIR="$PROJECT_DIR/dist"
VERSION="$(sed -n 's/^project(termify VERSION \([^ ]*\).*/\1/p' \
  "$PROJECT_DIR/CMakeLists.txt")"
source "$PROJECT_DIR/scripts/platform.sh"
source "$PROJECT_DIR/scripts/yt-dlp.sh"

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "Debian packages can only be built on Linux." >&2
  exit 1
fi
if [[ -z "$VERSION" ]]; then
  echo "Could not read the Termify version from CMakeLists.txt." >&2
  exit 1
fi
for command in cmake dpkg dpkg-deb dpkg-shlibdeps; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "$command is required to build a Debian package." >&2
    exit 1
  fi
done

# A native package is built against one distribution ABI. Publishing separate
# Debian 12 and Ubuntu 24.04 assets keeps dependency metadata honest when their
# FFmpeg and ALSA SONAMEs differ.
OS_ID="$(. /etc/os-release; printf '%s' "${ID:-}")"
OS_VERSION_ID="$(. /etc/os-release; printf '%s' "${VERSION_ID:-}")"
case "$OS_ID:$OS_VERSION_ID" in
debian:12) DISTRIBUTION=debian12 ;;
ubuntu:24.04) DISTRIBUTION=ubuntu24.04 ;;
*)
  echo "Debian packaging is supported on Debian 12 and Ubuntu 24.04; found ${OS_ID:-unknown} ${OS_VERSION_ID:-unknown}." >&2
  exit 1
  ;;
esac

DEB_ARCH="$(dpkg --print-architecture)"
case "$DEB_ARCH" in
amd64) RELEASE_ARCH=x86_64 ;;
arm64) RELEASE_ARCH=arm64 ;;
*)
  echo "Debian packaging is unsupported for architecture $DEB_ARCH." >&2
  exit 1
  ;;
esac
if ! termify_arch_matches_host "$RELEASE_ARCH"; then
  echo "dpkg architecture $DEB_ARCH does not match native host $(uname -m)." >&2
  exit 1
fi

PACKAGE_BASENAME="termify-v${VERSION}-${DISTRIBUTION}-${DEB_ARCH}"
PACKAGE="$DIST_DIR/$PACKAGE_BASENAME.deb"
WORK_DIR="$DIST_DIR/deb-work-${DISTRIBUTION}-${DEB_ARCH}"
PACKAGE_ROOT="$WORK_DIR/debian/termify"
YTDLP_CACHE="$(termify_ensure_ytdlp "$PROJECT_DIR")"

cmake -E remove_directory "$BUILD_DIR"
cmake -E remove_directory "$WORK_DIR"
cmake -E make_directory "$PACKAGE_ROOT"

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure

install_file() {
  local mode="$1"
  local source="$2"
  local destination="$3"
  mkdir -p "$(dirname "$destination")"
  install -m "$mode" "$source" "$destination"
}

install_file 755 "$BUILD_DIR/termify" "$PACKAGE_ROOT/usr/bin/termify"
if command -v strip >/dev/null 2>&1; then
  strip --strip-unneeded "$PACKAGE_ROOT/usr/bin/termify"
fi
install_file 755 "$YTDLP_CACHE" "$PACKAGE_ROOT/usr/lib/termify/yt-dlp"
install_file 644 "$PROJECT_DIR/packaging/appimage/termify.desktop" \
  "$PACKAGE_ROOT/usr/share/applications/termify.desktop"
install_file 644 "$PROJECT_DIR/packaging/appimage/termify.svg" \
  "$PACKAGE_ROOT/usr/share/icons/hicolor/scalable/apps/termify.svg"
install_file 644 "$PROJECT_DIR/packaging/deb/copyright" \
  "$PACKAGE_ROOT/usr/share/doc/termify/copyright"
install_file 644 "$PROJECT_DIR/README.md" \
  "$PACKAGE_ROOT/usr/share/doc/termify/README.md"
install_file 644 "$PROJECT_DIR/ROADMAP.md" \
  "$PACKAGE_ROOT/usr/share/doc/termify/ROADMAP.md"
for document in PLATFORM RELEASE TUI UPDATES; do
  install_file 644 "$PROJECT_DIR/docs/$document.md" \
    "$PACKAGE_ROOT/usr/share/doc/termify/$document.md"
done

# dpkg-shlibdeps resolves the exact runtime library package names for the
# distribution used to build this asset.
mkdir -p "$WORK_DIR/debian"
cat >"$WORK_DIR/debian/control" <<EOF
Source: termify
Section: sound
Priority: optional
Maintainer: Termify Maintainers <segfaultcafe@users.noreply.github.com>

Package: termify
Architecture: $DEB_ARCH
Description: terminal audio player for local and online music
EOF
SHLIBS_OUTPUT="$(
  cd "$WORK_DIR"
  dpkg-shlibdeps -O debian/termify/usr/bin/termify
)"
SHLIBS_DEPENDS="${SHLIBS_OUTPUT#shlibs:Depends=}"
if [[ -z "$SHLIBS_DEPENDS" || "$SHLIBS_DEPENDS" == "$SHLIBS_OUTPUT" ]]; then
  echo "dpkg-shlibdeps did not produce runtime dependencies." >&2
  exit 1
fi

INSTALLED_SIZE="$(du -sk "$PACKAGE_ROOT" | awk '{print $1}')"
mkdir -p "$PACKAGE_ROOT/DEBIAN"
cat >"$PACKAGE_ROOT/DEBIAN/control" <<EOF
Package: termify
Version: $VERSION
Section: sound
Priority: optional
Architecture: $DEB_ARCH
Maintainer: Termify Maintainers <segfaultcafe@users.noreply.github.com>
Homepage: https://github.com/segfaultcafe/Termify
Installed-Size: $INSTALLED_SIZE
Depends: $SHLIBS_DEPENDS, ca-certificates, curl
Description: terminal audio player for local and online music
 Termify plays local files and online audio in an interactive terminal UI.
 This package includes a verified standalone yt-dlp executable.
EOF

mkdir -p "$DIST_DIR"
rm -f -- "$PACKAGE" "$PACKAGE.sha256"
dpkg-deb --build --root-owner-group "$PACKAGE_ROOT" "$PACKAGE"
termify_write_sha256 "$PACKAGE" "$PACKAGE.sha256"

"$PACKAGE_ROOT/usr/bin/termify" --version
"$PACKAGE_ROOT/usr/lib/termify/yt-dlp" --version
echo "Created $PACKAGE"
echo "Created $PACKAGE.sha256"
