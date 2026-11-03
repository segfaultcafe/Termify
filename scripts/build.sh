#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/bin/Debug"
source "$PROJECT_DIR/scripts/yt-dlp.sh"
YTDLP_SOURCE="$(termify_ensure_ytdlp "$PROJECT_DIR")"

if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  CACHED_PROJECT_DIR="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' \
    "$BUILD_DIR/CMakeCache.txt")"
  if [[ "$CACHED_PROJECT_DIR" != "$PROJECT_DIR" ]]; then
    cmake -E remove_directory "$BUILD_DIR"
  fi
fi

CMAKE_ARGUMENTS=(
  -S "$PROJECT_DIR"
  -B "$BUILD_DIR"
  -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  -DBUILD_TESTING=ON
)
if [[ "$(uname -s)" == "Darwin" ]] && command -v brew >/dev/null 2>&1; then
  CMAKE_ARGUMENTS+=("-DCMAKE_PREFIX_PATH=$(brew --prefix)")
fi
cmake "${CMAKE_ARGUMENTS[@]}"

cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure

ln -sfn "./bin/Debug/compile_commands.json" \
  "$PROJECT_DIR/compile_commands.json"
mkdir -p "$BUILD_DIR/.local/bin"
install -m 755 "$YTDLP_SOURCE" "$BUILD_DIR/.local/bin/yt-dlp"

echo "Bundled yt-dlp: $("$BUILD_DIR/.local/bin/yt-dlp" --version)"

echo "Built: $BUILD_DIR/termify"
