#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$(mktemp -d /tmp/vosc-performance-tests.XXXXXX)}"
CXX="${CXX:-clang++}"
FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic -fno-omit-frame-pointer)
if [[ -n "${SANITIZER:-}" ]]; then
  FLAGS+=("-fsanitize=$SANITIZER")
fi

mkdir -p "$BUILD_DIR"
"$CXX" "${FLAGS[@]}" -I"$ROOT_DIR/src" \
  "$ROOT_DIR/tests/performance/PerformanceTests.cpp" \
  "$ROOT_DIR/src/control/PerformanceTelemetry.cpp" \
  -o "$BUILD_DIR/PerformanceTests"
"$BUILD_DIR/PerformanceTests"

"$CXX" "${FLAGS[@]}" -I"$ROOT_DIR/tests/performance/stubs" -I"$ROOT_DIR/src" \
  "$ROOT_DIR/tests/performance/PerformanceOpenGlTests.cpp" \
  "$ROOT_DIR/src/control/PerformanceOpenGl.cpp" \
  -o "$BUILD_DIR/PerformanceOpenGlTests"
"$BUILD_DIR/PerformanceOpenGlTests"
