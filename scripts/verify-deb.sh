#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$PROJECT_DIR/scripts/yt-dlp.sh"

if (( $# != 2 )); then
  echo "Usage: verify-deb.sh DISTRIBUTION ARCHITECTURE" >&2
  exit 2
fi

DISTRIBUTION="$1"
case "$2" in
amd64 | x86_64) DEB_ARCH=amd64 ;;
arm64 | aarch64) DEB_ARCH=arm64 ;;
*) echo "Unsupported Debian architecture: $2" >&2; exit 2 ;;
esac
case "$DISTRIBUTION" in
debian12 | ubuntu24.04) ;;
*) echo "Unsupported Debian distribution: $DISTRIBUTION" >&2; exit 2 ;;
esac

VERSION="$(sed -n 's/^project(termify VERSION \([^ ]*\).*/\1/p' \
  "$PROJECT_DIR/CMakeLists.txt")"
PACKAGE_NAME="termify-v${VERSION}-${DISTRIBUTION}-${DEB_ARCH}.deb"
PACKAGE="$PROJECT_DIR/dist/$PACKAGE_NAME"
CHECKSUM="$PACKAGE.sha256"

if [[ -z "$VERSION" || ! -f "$PACKAGE" || ! -f "$CHECKSUM" ]]; then
  echo "The expected $PACKAGE_NAME and checksum were not produced." >&2
  exit 1
fi

read -r EXPECTED_DIGEST CHECKSUM_NAME CHECKSUM_EXTRA <"$CHECKSUM"
if [[ -n "${CHECKSUM_EXTRA:-}" || "$CHECKSUM_NAME" != "$PACKAGE_NAME" ]]; then
  echo "The checksum file does not name exactly $PACKAGE_NAME." >&2
  exit 1
fi
termify_verify_sha256 "$EXPECTED_DIGEST" "$PACKAGE"

test "$(dpkg-deb --field "$PACKAGE" Package)" = termify
test "$(dpkg-deb --field "$PACKAGE" Version)" = "$VERSION"
test "$(dpkg-deb --field "$PACKAGE" Architecture)" = "$DEB_ARCH"
DEPENDENCIES="$(dpkg-deb --field "$PACKAGE" Depends)"
for dependency in ca-certificates curl libc6; do
  DEPENDENCY_PATTERN="(^|, )${dependency}([[:space:](),]|$)"
  if [[ ! $DEPENDENCIES =~ $DEPENDENCY_PATTERN ]]; then
    echo "$PACKAGE_NAME does not declare $dependency." >&2
    exit 1
  fi
done

SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/termify-deb.XXXXXX")"
trap 'rm -rf -- "$SCRATCH"' EXIT
dpkg-deb --extract "$PACKAGE" "$SCRATCH/root"

for executable in usr/bin/termify usr/lib/termify/yt-dlp; do
  if [[ ! -x "$SCRATCH/root/$executable" ]]; then
    echo "$executable is missing or not executable in $PACKAGE_NAME." >&2
    exit 1
  fi
done
for file in \
  usr/share/applications/termify.desktop \
  usr/share/icons/hicolor/scalable/apps/termify.svg \
  usr/share/doc/termify/copyright; do
  if [[ ! -f "$SCRATCH/root/$file" ]]; then
    echo "$file is missing from $PACKAGE_NAME." >&2
    exit 1
  fi
done

test "$("$SCRATCH/root/usr/bin/termify" --version)" = "termify $VERSION"
test "$("$SCRATCH/root/usr/lib/termify/yt-dlp" --version)" = \
  "$TERMIFY_YTDLP_VERSION"
if ldd "$SCRATCH/root/usr/bin/termify" | grep -q 'not found'; then
  echo "$PACKAGE_NAME contains an executable with unresolved libraries." >&2
  exit 1
fi

echo "Verified $PACKAGE_NAME metadata, contents, checksum, and executables."
