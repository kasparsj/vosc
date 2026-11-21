#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/tests/bin}"
CXX="${CXX:-clang++}"
SANITIZER="${SANITIZER:-}"
OF_ROOT="$(python3 "$ROOT_DIR/scripts/build_macos_profile.py" --sdk-root)"

mkdir -p "$BUILD_DIR"

CXXFLAGS=(
  -std=c++17
  -O1
  -g
  -Wall
  -Wextra
  -pedantic
  -fno-omit-frame-pointer
  -I"$ROOT_DIR/tests/stubs"
  -I"$ROOT_DIR/src"
)

LDFLAGS=()

if [[ -n "$SANITIZER" ]]; then
  CXXFLAGS+=("-fsanitize=$SANITIZER")
  LDFLAGS+=("-fsanitize=$SANITIZER")
fi

if [[ -n "$OF_ROOT" && -d "$OF_ROOT/libs/json/include" ]]; then
  CXXFLAGS+=("-I$OF_ROOT/libs/json/include")
fi

compile_test() {
  local name="$1"
  shift
  local output="$BUILD_DIR/$name"
  echo "Compiling $name"
  if [[ ${#LDFLAGS[@]} -gt 0 ]]; then
    "$CXX" "${CXXFLAGS[@]}" "$@" -o "$output" "${LDFLAGS[@]}"
  else
    "$CXX" "${CXXFLAGS[@]}" "$@" -o "$output"
  fi
}

compile_test \
  CommandParserTests \
  "$ROOT_DIR/tests/osc/CommandParserTests.cpp" \
  "$ROOT_DIR/src/osc/ArgReader.cpp" \
  "$ROOT_DIR/src/osc/CommandParser.cpp" \
  "$ROOT_DIR/src/osc/CommandRouter.cpp"

compile_test \
  CommandDispatchTests \
  "$ROOT_DIR/tests/osc/CommandDispatchTests.cpp" \
  "$ROOT_DIR/src/osc/ArgReader.cpp" \
  "$ROOT_DIR/src/osc/CommandParser.cpp" \
  "$ROOT_DIR/src/osc/CommandRouter.cpp"

compile_test \
  ArgsTests \
  "$ROOT_DIR/tests/osc/ArgsTests.cpp" \
  "$ROOT_DIR/src/Args.cpp"

echo "Running CommandParserTests"
"$BUILD_DIR/CommandParserTests"

echo "Running CommandDispatchTests"
"$BUILD_DIR/CommandDispatchTests"

echo "Running ArgsTests"
"$BUILD_DIR/ArgsTests"

echo "All OSC tests passed"
