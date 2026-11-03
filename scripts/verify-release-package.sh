#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$PROJECT_DIR/scripts/yt-dlp.sh"

if (( $# != 2 )); then
  echo "Usage: verify-release-package.sh PLATFORM ARCHITECTURE" >&2
  exit 2
fi

PLATFORM="$1"
ARCHITECTURE="$2"
VERSION="$(sed -n 's/^project(termify VERSION \([^ ]*\).*/\1/p' \
  "$PROJECT_DIR/CMakeLists.txt")"
PACKAGE="termify-v${VERSION}-${PLATFORM}-${ARCHITECTURE}"
ARCHIVE="$PROJECT_DIR/dist/$PACKAGE.tar.gz"
CHECKSUM="$ARCHIVE.sha256"

if [[ -z "$VERSION" || ! -f "$ARCHIVE" || ! -f "$CHECKSUM" ]]; then
  echo "The expected $PACKAGE archive and checksum were not produced." >&2
  exit 1
fi
if [[ "$(uname -m)" != "$ARCHITECTURE" ]]; then
  echo "Cannot execute an $ARCHITECTURE package on $(uname -m)." >&2
  exit 1
fi

read -r EXPECTED_DIGEST CHECKSUM_NAME CHECKSUM_EXTRA <"$CHECKSUM"
if [[ -n "${CHECKSUM_EXTRA:-}" || "$CHECKSUM_NAME" != "$PACKAGE.tar.gz" ]]; then
  echo "The checksum file does not name exactly $PACKAGE.tar.gz." >&2
  exit 1
fi
if ! termify_verify_sha256 "$EXPECTED_DIGEST" "$ARCHIVE"; then
  echo "Checksum verification failed for $ARCHIVE." >&2
  exit 1
fi

while IFS= read -r member; do
  if [[ "$member" != "$PACKAGE" && "$member" != "$PACKAGE/"* ]] ||
    [[ "$member" == */../* || "$member" == */.. ]]; then
    echo "Unsafe or unexpected archive member: $member" >&2
    exit 1
  fi
done < <(tar -tzf "$ARCHIVE")

SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/termify-package.XXXXXX")"
trap 'rm -rf -- "$SCRATCH"' EXIT
tar -xzf "$ARCHIVE" -C "$SCRATCH"
EXTRACTED="$SCRATCH/$PACKAGE"

for file in termify yt-dlp install.sh; do
  if [[ ! -x "$EXTRACTED/$file" ]]; then
    echo "$file is missing or not executable in $ARCHIVE." >&2
    exit 1
  fi
done

if [[ "$("$EXTRACTED/termify" --version)" != "termify $VERSION" ]]; then
  echo "The packaged Termify executable reports the wrong version." >&2
  exit 1
fi
if [[ "$("$EXTRACTED/yt-dlp" --version)" != "$TERMIFY_YTDLP_VERSION" ]]; then
  echo "The packaged yt-dlp executable reports the wrong version." >&2
  exit 1
fi

PREFIX="$SCRATCH/install"
"$EXTRACTED/install.sh" "$PREFIX"
test "$("$PREFIX/bin/termify" --version)" = "termify $VERSION"
test "$("$PREFIX/lib/termify/yt-dlp" --version)" = \
  "$TERMIFY_YTDLP_VERSION"

echo "Verified $PACKAGE and its installer."
