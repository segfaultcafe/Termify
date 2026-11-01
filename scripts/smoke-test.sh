#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 /path/to/termify" >&2
  exit 2
fi

binary="$1"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/termify-smoke.XXXXXX")"
trap 'rm -rf -- "$scratch"' EXIT HUP INT TERM

mkdir -p "$scratch/home" "$scratch/data" "$scratch/cache"
export HOME="$scratch/home"
export XDG_DATA_HOME="$scratch/data"
export XDG_CACHE_HOME="$scratch/cache"
export TERMIFY_YTDLP="$scratch/missing-yt-dlp"
export PATH="/usr/bin:/bin"

"$binary" --version
printf 'exit\n' >"$scratch/input"
"$binary" <"$scratch/input" >"$scratch/stdout" 2>"$scratch/stderr"
