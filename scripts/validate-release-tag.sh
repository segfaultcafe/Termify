#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if (( $# != 3 )); then
  echo "Usage: validate-release-tag.sh TAG EVENT_SHA RELEASE_BRANCH_REF" >&2
  exit 2
fi

TAG="$1"
EVENT_SHA="$2"
RELEASE_BRANCH_REF="$3"

if [[ ! "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Release tags must use the stable vMAJOR.MINOR.PATCH format: $TAG" >&2
  exit 1
fi

VERSION="$(sed -n 's/^project(termify VERSION \([^ ]*\).*/\1/p' \
  "$PROJECT_DIR/CMakeLists.txt")"
if [[ -z "$VERSION" ]]; then
  echo "Could not read the Termify version from CMakeLists.txt." >&2
  exit 1
fi
if [[ "$TAG" != "v$VERSION" ]]; then
  echo "Tag $TAG does not match the CMake project version v$VERSION." >&2
  exit 1
fi

cd "$PROJECT_DIR"
TAG_REF="refs/tags/$TAG"
if ! git rev-parse --verify --quiet "$TAG_REF" >/dev/null; then
  echo "Tag $TAG is not present in the checkout." >&2
  exit 1
fi
if ! git rev-parse --verify --quiet "$RELEASE_BRANCH_REF^{commit}" >/dev/null; then
  echo "Release branch ref $RELEASE_BRANCH_REF is not present." >&2
  exit 1
fi

TAG_OBJECT="$(git rev-parse "$TAG_REF")"
TAG_COMMIT="$(git rev-parse "$TAG_REF^{commit}")"
CHECKED_OUT_COMMIT="$(git rev-parse HEAD^{commit})"

# GitHub may report either the tag object (annotated tags) or the peeled commit
# for a tag push. Both must resolve to the exact commit checked out below.
if [[ "$EVENT_SHA" != "$TAG_OBJECT" && "$EVENT_SHA" != "$TAG_COMMIT" ]]; then
  echo "The event SHA does not identify $TAG." >&2
  exit 1
fi
if [[ "$CHECKED_OUT_COMMIT" != "$TAG_COMMIT" ]]; then
  echo "The checkout is not the commit referenced by $TAG." >&2
  exit 1
fi
if ! git merge-base --is-ancestor "$TAG_COMMIT" "$RELEASE_BRANCH_REF"; then
  echo "$TAG does not point to a commit on $RELEASE_BRANCH_REF." >&2
  exit 1
fi

printf 'Validated %s at %s (Termify %s).\n' \
  "$TAG" "$TAG_COMMIT" "$VERSION"
