#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$(mktemp -d /tmp/vosc-control-tests.XXXXXX)}"
CXX="${CXX:-clang++}"
CHECK_FLAGS=(-fno-omit-frame-pointer)
if [[ -n "${SANITIZER:-}" ]]; then
  CHECK_FLAGS+=("-fsanitize=$SANITIZER")
fi

mkdir -p "$BUILD_DIR"

"$CXX" \
  -std=c++17 \
  -O1 \
  -g \
  -Wall \
  -Wextra \
  -Werror \
  -pedantic \
  "${CHECK_FLAGS[@]}" \
  -I"$ROOT_DIR/src" \
  "$ROOT_DIR/tests/control/LocalControlServerTests.cpp" \
  "$ROOT_DIR/src/control/LocalControlServer.cpp" \
  -o "$BUILD_DIR/LocalControlServerTests"

"$BUILD_DIR/LocalControlServerTests"
