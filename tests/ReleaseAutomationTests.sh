#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/termify-release-test.XXXXXX")"
trap 'rm -rf -- "$SCRATCH"' EXIT

REPOSITORY="$SCRATCH/repository"
mkdir -p "$REPOSITORY/scripts"
cp "$PROJECT_DIR/scripts/validate-release-tag.sh" "$REPOSITORY/scripts/"
cd "$REPOSITORY"
git init --quiet --initial-branch=master
git config user.name "Termify Tests"
git config user.email "tests@termify.invalid"
printf '%s\n' \
  'cmake_minimum_required(VERSION 3.16)' \
  'project(termify VERSION 1.2.3 LANGUAGES C CXX)' >CMakeLists.txt
git add CMakeLists.txt scripts/validate-release-tag.sh
git commit --quiet -m "fixture"
git update-ref refs/remotes/origin/master HEAD
git tag -a v1.2.3 -m "fixture release"

TAG_OBJECT="$(git rev-parse refs/tags/v1.2.3)"
TAG_COMMIT="$(git rev-parse refs/tags/v1.2.3^{commit})"
VALIDATOR="$REPOSITORY/scripts/validate-release-tag.sh"

"$VALIDATOR" v1.2.3 "$TAG_OBJECT" refs/remotes/origin/master
"$VALIDATOR" v1.2.3 "$TAG_COMMIT" refs/remotes/origin/master

git tag v1.2.4
if "$VALIDATOR" v1.2.4 "$(git rev-parse refs/tags/v1.2.4)" \
  refs/remotes/origin/master >/dev/null 2>&1; then
  echo "A tag/CMake version mismatch was accepted." >&2
  exit 1
fi
if "$VALIDATOR" v1.2.3 "0000000000000000000000000000000000000000" \
  refs/remotes/origin/master >/dev/null 2>&1; then
  echo "An unrelated event SHA was accepted." >&2
  exit 1
fi

git switch --quiet -c feature
sed -i.bak 's/VERSION 1.2.3/VERSION 1.3.0/' CMakeLists.txt
rm CMakeLists.txt.bak
git add CMakeLists.txt
git commit --quiet -m "off-branch release"
git tag v1.3.0
if "$VALIDATOR" v1.3.0 "$(git rev-parse refs/tags/v1.3.0)" \
  refs/remotes/origin/master >/dev/null 2>&1; then
  echo "A release commit outside the release branch was accepted." >&2
  exit 1
fi

echo "Release tag validation tests passed."
