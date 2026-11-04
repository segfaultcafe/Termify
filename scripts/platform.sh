#!/usr/bin/env bash

termify_canonical_arch() {
  case "$1" in
  x86_64 | amd64) printf '%s\n' x86_64 ;;
  aarch64 | arm64) printf '%s\n' arm64 ;;
  *)
    echo "Unsupported architecture: $1" >&2
    return 1
    ;;
  esac
}

termify_appimage_arch() {
  case "$(termify_canonical_arch "$1")" in
  x86_64) printf '%s\n' x86_64 ;;
  arm64) printf '%s\n' aarch64 ;;
  esac
}

termify_arch_matches_host() {
  local expected actual
  expected="$(termify_canonical_arch "$1")" || return 1
  actual="$(termify_canonical_arch "$(uname -m)")" || return 1
  [[ "$actual" == "$expected" ]]
}
