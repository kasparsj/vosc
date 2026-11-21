#!/usr/bin/env bash
# Keep this process running while agents author through MCP.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export VOSC_CONTROL_SOCKET="${VOSC_CONTROL_SOCKET:-$HOME/.local/share/vosc/control.sock}"

if [[ -n "${VOSC_BUILD_DIR:-}" && "$VOSC_BUILD_DIR" != /* ]]; then
  echo "VOSC_BUILD_DIR must be an absolute path" >&2
  exit 1
fi
if [[ "$VOSC_CONTROL_SOCKET" != /* ]]; then
  echo "VOSC_CONTROL_SOCKET must be an absolute path" >&2
  exit 1
fi
CONTROL_DIR="$(dirname "$VOSC_CONTROL_SOCKET")"
if [[ ! -d "$CONTROL_DIR" ]]; then
  (umask 077; mkdir -p "$CONTROL_DIR")
fi
if [[ -e "$VOSC_CONTROL_SOCKET" || -L "$VOSC_CONTROL_SOCKET" ]]; then
  echo "Control socket already exists: $VOSC_CONTROL_SOCKET" >&2
  echo "Use the running renderer, or close it before starting another." >&2
  exit 1
fi
if lsof -nP -iUDP:33333 >/dev/null 2>&1; then
  echo "UDP 33333 is occupied; leave the existing process intact." >&2
  exit 1
fi
if [[ -n "${VOSC_BUILD_DIR:-}" ]]; then
  BUILD_DIR="$VOSC_BUILD_DIR"
else
  SDK_VERSION="$(python3 "$ROOT_DIR/scripts/build_macos_profile.py" --sdk-version)"
  BUILD_DIR="$HOME/Library/Caches/vosc-authoring/build-of-$SDK_VERSION"
fi
APP="$BUILD_DIR/products/voscDebug.app/Contents/MacOS/voscDebug"
if [[ ! -x "$APP" ]]; then
  python3 "$ROOT_DIR/scripts/build_macos_authoring.py" --output "$BUILD_DIR"
fi
printf 'VOSC control socket: %s\n' "$VOSC_CONTROL_SOCKET"
cd "$BUILD_DIR/products"
exec "$APP"
