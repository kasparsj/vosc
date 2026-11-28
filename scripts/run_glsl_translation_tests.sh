#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  printf 'Usage: %s GLSLANG TINT [CURRENT_HYDRA_GLSL]\n' "$0" >&2
  exit 2
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/vosc-translation-tests.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT
flags=(-std=c++23 -DVOSC_WEBGPU_M1 -O1 -Wall -Wextra -Werror -pthread)
libraries=()
if [[ "$(uname -s)" == Darwin ]]; then
  sdk="$(python3 "$root/scripts/build_macos_profile.py" --sdk-root)"
  flags+=(-I"$sdk/libs/json/include" -Wno-deprecated-declarations)
else
  libraries+=(-lcrypto)
fi
"${CXX:-c++}" "${flags[@]}" -I"$root/src" \
  "$root/tests/webgpu/GlslTranslationTests.cpp" "$root/src/webgpu/GlslTranslation.cpp" \
  "${libraries[@]}" -o "$scratch/GlslTranslationTests"

python3 - "$scratch" "$root" "$1" "$2" <<'PY'
import base64
import hashlib
import json
from pathlib import Path
import shutil
import sys

scratch, root, glslang, tint = map(Path, sys.argv[1:])
versions = {"glslang": "15.1.0", "tint": "e4775a0d00343d02d32daecd45e1eb683224214b"}
for package in ("real", "slow", "large", "mismatch"):
    tools = scratch / package
    tools.mkdir()
    manifest = {"format": "vosc.webgpu-tools", "version": 1}
    for name, source in (("glslang", glslang), ("tint", tint)):
        binary = tools / name
        supplied = source if package == "real" or name == "tint" else scratch / "GlslTranslationTests"
        shutil.copy2(supplied, binary)
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        if package == "mismatch" and name == "glslang":
            digest = "0" * 64
        manifest[name] = {"file": name, "version": versions[name], "sha256": digest}
    (tools / "manifest.json").write_text(json.dumps(manifest))
fixtures = scratch / "fixtures"
fixtures.mkdir()
shutil.copy2(root / "examples/shaders/assets/glow-ribbons.frag", fixtures / "glow-ribbons.frag")
legacy = root / "desktop/authoring/tests/fixtures/hydra/legacy-m4.vosc-draft"
source = json.loads(legacy.read_text())["accepted"]["project"]["source"]["base64"]
(fixtures / "legacy-hydra.frag").write_bytes(base64.b64decode(source))
PY

if [[ $# == 3 ]]; then
  cp "$3" "$scratch/fixtures/current-hydra.frag"
fi

"$scratch/GlslTranslationTests" "$scratch/real" "$scratch/fixtures" "$scratch"
