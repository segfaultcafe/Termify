#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$PROJECT_DIR/scripts/yt-dlp.sh"

if (( $# != 1 )); then
  echo "Usage: verify-deb-install.sh /path/to/termify.deb" >&2
  exit 2
fi
if [[ "$(id -u)" != 0 ]]; then
  echo "The clean Debian package installation test must run as root." >&2
  exit 1
fi

if [[ ! -f "$1" ]]; then
  echo "Debian package not found: $1" >&2
  exit 1
fi
PACKAGE="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
VERSION="$(dpkg-deb --field "$PACKAGE" Version)"
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
  "$PACKAGE"

test "$(dpkg-query --show --showformat='${Status}' termify)" = \
  "install ok installed"
test "$(termify --version)" = "termify $VERSION"
test "$(/usr/lib/termify/yt-dlp --version)" = "$TERMIFY_YTDLP_VERSION"
sh "$PROJECT_DIR/scripts/smoke-test.sh" /usr/bin/termify

SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/termify-deb-install.XXXXXX")"
trap 'rm -rf -- "$SCRATCH"' EXIT
if termify update --rollback >"$SCRATCH/stdout" 2>"$SCRATCH/stderr"; then
  echo "A package-managed installation incorrectly accepted self-update." >&2
  exit 1
fi
if ! grep -qi 'managed by a package manager' \
  "$SCRATCH/stdout" "$SCRATCH/stderr"; then
  echo "The updater did not explain package-manager ownership." >&2
  exit 1
fi
if termify update yt-dlp --check >"$SCRATCH/stdout" 2>"$SCRATCH/stderr"; then
  echo "A package-managed installation accepted a private yt-dlp update." >&2
  exit 1
fi
if ! grep -qi 'managed by a package manager' \
  "$SCRATCH/stdout" "$SCRATCH/stderr"; then
  echo "The yt-dlp updater did not explain package-manager ownership." >&2
  exit 1
fi

DEBIAN_FRONTEND=noninteractive apt-get remove -y termify
test ! -e /usr/bin/termify
test ! -e /usr/lib/termify/yt-dlp

echo "Installed, launched, update-guarded, and removed $(basename "$PACKAGE")."
