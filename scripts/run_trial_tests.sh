#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$(mktemp -d /tmp/vosc-trial-tests.XXXXXX)}"
CXX="${CXX:-clang++}"
FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic -fno-omit-frame-pointer)
if [[ -n "${SANITIZER:-}" ]]; then
  FLAGS+=("-fsanitize=$SANITIZER")
fi

mkdir -p "$BUILD_DIR"
"$CXX" "${FLAGS[@]}" -I"$ROOT_DIR/src" \
  "$ROOT_DIR/tests/trial/ShaderTrialPolicyTests.cpp" \
  "$ROOT_DIR/src/control/ShaderTrialPolicy.cpp" \
  -o "$BUILD_DIR/ShaderTrialPolicyTests"
"$BUILD_DIR/ShaderTrialPolicyTests"
