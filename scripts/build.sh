#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/bin/Debug"

if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  CACHED_PROJECT_DIR="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' \
    "$BUILD_DIR/CMakeCache.txt")"
  if [[ "$CACHED_PROJECT_DIR" != "$PROJECT_DIR" ]]; then
    cmake -E remove_directory "$BUILD_DIR"
  fi
fi

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DBUILD_TESTING=ON

cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure

ln -sfn "./bin/Debug/compile_commands.json" \
  "$PROJECT_DIR/compile_commands.json"
install -Dm755 "$PROJECT_DIR/ext/yt-dlp" \
  "$BUILD_DIR/.local/bin/yt-dlp"

echo "Built: $BUILD_DIR/termify"
