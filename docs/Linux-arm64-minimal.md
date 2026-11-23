# Minimal Linux ARM64 build

Start in a clean checkout of a VOSC revision containing this guide. The build
uses an Ubuntu 24.04 ARM64 container and extracts a fresh openFrameworks 0.12.1
SDK inside it. Docker must support running Linux ARM64 containers.

Run this from the checkout on the host.

```bash
docker run --rm -it --platform linux/arm64 \
  --mount "type=bind,source=$PWD,target=/spike/source" \
  --workdir /spike/source \
  ubuntu@sha256:9c7f200c07e3d4a03e7249d2c16bf66a51120fb876aa2242cf31451412543289 \
  bash
```

Run the remaining blocks in that container, in the same Bash shell. Use the
official [openFrameworks 0.12.1 ARM64 SDK](https://github.com/openframeworks/openFrameworks/releases/tag/0.12.1).
The SDK includes ofxAssimpModelLoader and ofxOsc. The other retained addons and
the float muParser fork use the URLs and SHA-256 checksums in
[`scripts/setup_macos_dependencies.json`](../scripts/setup_macos_dependencies.json).

## Install dependencies and extract the SDK

```bash
set -euo pipefail
test "$(uname -m)" = aarch64
cat /etc/os-release
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  build-essential pkg-config python3 curl ca-certificates binutils cmake \
  nlohmann-json3-dev libcurl4-openssl-dev libjack-jackd2-dev libasound2-dev \
  libxmu-dev libxxf86vm-dev libgl1-mesa-dev libglu1-mesa-dev libglut-dev \
  libraw1394-dev libudev-dev libdrm-dev libglew-dev libopenal-dev libsndfile1-dev \
  libfreeimage-dev libcairo2-dev libfreetype6-dev libssl-dev libpulse-dev \
  libusb-1.0-0-dev libgtk-3-dev libassimp-dev librtaudio-dev libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev libglfw3-dev liburiparser-dev libpugixml-dev \
  libpoco-dev libxcursor-dev libxi-dev libxinerama-dev libxml2-dev libharfbuzz-dev \
  libunwind-dev

python3 - <<'PY'
import hashlib
import json
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

pins = json.loads(Path("scripts/setup_macos_dependencies.json").read_text())
sdk = Path("/opt/openFrameworks")
sdk.mkdir()
muparser = Path("/tmp/vosc-muparser")
muparser.mkdir()

# An optional read-only archive cache can supply the same checksum-verified inputs.
cache_path = os.environ.get("VOSC_ARCHIVE_CACHE")
cache = Path(cache_path) if cache_path else None


def extract(name, pin, destination, downloads):
    archive = cache / name if cache else None
    if archive is None or not archive.is_file():
        archive = downloads / name
        subprocess.run(["curl", "-fsSL", "-o", str(archive), pin["url"]], check=True)
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != pin["sha256"]:
        raise SystemExit(f"SHA-256 mismatch for {name}: {digest}")
    subprocess.run(
        ["tar", "-xzf", str(archive), "--strip-components=1", "-C", str(destination)],
        check=True,
    )


with TemporaryDirectory(prefix="vosc-downloads-") as directory:
    downloads = Path(directory)
    extract(
        "openFrameworks.tgz",
        {
            "url": "https://github.com/openframeworks/openFrameworks/releases/download/0.12.1/"
                   "of_v0.12.1_linuxaarch64_release.tar.gz",
            "sha256": "15ed36029e5316822370cc4dc1f17a2b4c9dc43e416be67719088b7e755be802",
        },
        sdk,
        downloads,
    )
    for name in (
        "ofxAutoReloadedShader", "ofxColorTheory", "ofxEasing", "ofxExpr",
        "ofxLooper", "ofxMidi", "ofxPostProcessing", "ofxShadertoy", "ofxTidalCycles",
    ):
        addon = sdk / "addons" / name
        addon.mkdir()
        extract(f"{name}.tgz", pins["addons"][name], addon, downloads)
    extract("muparser.tgz", pins["muparser_source"], muparser, downloads)
PY
```

To reuse local archives, add a read-only bind mount to the Docker command
and set `VOSC_ARCHIVE_CACHE` to its container path. Use the `.tgz` names
above. Each archive is checked before extraction.

## Build and verify the executable

ofxExpr expects the fork's float ABI. Build the static library in the ARM64
container and install it at ofxExpr's `lib/linuxaarch64/libmuparser.a` path.

```bash
expr=/opt/openFrameworks/addons/ofxExpr/libs/muparser
diff -qr "$expr/include" /tmp/vosc-muparser/include
cmake -S /tmp/vosc-muparser -B /tmp/vosc-muparser-build \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=ON -DENABLE_SAMPLES=OFF -DENABLE_OPENMP=OFF
cmake --build /tmp/vosc-muparser-build --parallel 4
ctest --test-dir /tmp/vosc-muparser-build --output-on-failure
install -D -m 644 /tmp/vosc-muparser-build/libmuparser.a "$expr/lib/linuxaarch64/libmuparser.a"

midi=/opt/openFrameworks/addons/ofxMidi
find "$midi" -type f -print0 | sort -z | xargs -0 sha256sum > /tmp/ofxMidi-before.sha256

python3 scripts/test_build_linux_minimal.py
export MAKEFLAGS=-j4
make Release VOSC_PROFILE=minimal OF_ROOT=/opt/openFrameworks | tee /tmp/vosc-build.log

sha256sum --quiet -c /tmp/ofxMidi-before.sha256
echo "PASS installed ofxMidi files match pre-build checksums"

stage=$(sed -n 's/^Building VOSC minimal in //p' /tmp/vosc-build.log)
binary="$stage/bin/vosc"
test -x "$binary"
readelf -h "$binary" | tee /tmp/vosc-readelf.txt
grep -E 'Machine:[[:space:]]+AArch64' /tmp/vosc-readelf.txt
sha256sum "$binary"
ldd "$binary" | tee /tmp/vosc-ldd.txt
if grep -q 'not found' /tmp/vosc-ldd.txt; then
  exit 1
fi
echo "PASS ARM64 link and dynamic library resolution"
```

The build stages its ofxMidi override with ALSA and JACK enabled and the iOS
and PGMidi sources excluded. The checksum check verifies that the SDK's
installed ofxMidi files keep their original contents.

## Smoke-test expression evaluation

Run Xvfb directly and use its allocated display. The smoke check launches
VOSC with a temporary copy of the editable-scene example, waits for the first
composed frame, and reads the layer alpha from the local control socket.

```bash
apt-get install -y --no-install-recommends xvfb libgl1-mesa-dri
display_file=$(mktemp)
Xvfb -displayfd 3 -screen 0 800x600x24 -nolisten tcp 3>"$display_file" &
xvfb_pid=$!
trap 'kill "$xvfb_pid" 2>/dev/null || true; rm -f "$display_file"' EXIT
for attempt in {1..50}; do
  if test -s "$display_file"; then
    break
  fi
  kill -0 "$xvfb_pid"
  sleep 0.1
done
test -s "$display_file"
DISPLAY=":$(cat "$display_file")" LIBGL_ALWAYS_SOFTWARE=1 \
  PYTHONPATH="$PWD/scripts" python3 - "$binary" <<'PY'
from contextlib import closing
import json
import math
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
from tempfile import TemporaryDirectory
import time

from control_client import Client

binary = Path(sys.argv[1]).resolve()
with TemporaryDirectory(prefix="vosc-expr-") as directory:
    runtime = Path(directory)
    scene_dir = runtime / "scene"
    shutil.copytree(binary.parent / "data/examples/editable-scene", scene_dir)
    manifest = scene_dir / "scene.vosc.json"
    scene = json.loads(manifest.read_text())
    alpha = next(cmd for cmd in scene["commands"] if cmd["address"] == "/layer/alpha")
    alpha["args"][1] = {"type": "s", "value": "0.125+0.25"}
    manifest.write_text(json.dumps(scene))

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    endpoint = runtime / "control.sock"
    environment = {**os.environ, "VOSC_CONTROL_SOCKET": str(endpoint), "VOSC_OSC_PORT": str(port)}
    log_path = binary.parent.parent / "expression-smoke-renderer.log"
    with log_path.open("wb") as log:
        renderer = subprocess.Popen(
            [str(binary), "--scene", str(manifest), "--size", "640x480", "--fps", "30"],
            cwd=binary.parent, env=environment, stdout=log, stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + 30
            observed = None
            while time.monotonic() < deadline:
                if renderer.poll() is not None:
                    raise SystemExit(f"Renderer exited {renderer.returncode}. See {log_path}")
                lines = log_path.read_text(errors="replace").splitlines()
                if endpoint.exists() and any(line.startswith("VOSC_SCENE_READY ") for line in lines):
                    with closing(Client(endpoint)) as client:
                        response = client.call("get_scene")
                    layers = response.get("result", {}).get("layers", [])
                    if len(layers) == 1:
                        observed = layers[0]["alpha"]
                        if layers[0]["shader"]["loaded"] and math.isclose(observed, 0.375, abs_tol=1e-6):
                            print("PASS VOSC evaluated 0.125+0.25 as layer alpha 0.375 under Xvfb software GL")
                            break
                time.sleep(0.1)
            else:
                raise SystemExit(f"Observed layer alpha {observed!r}. See {log_path}")
        finally:
            if renderer.poll() is None:
                renderer.terminate()
                try:
                    renderer.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    renderer.kill()
                    renderer.wait(timeout=8)
PY
```

## Verification status

The source tree in `d640cc2` passed a clean SDK build on Ubuntu 24.04.5 ARM64
on 2026-11-23 using checksum-verified cached archives. The muParser and
Linux staging tests passed, the executable linked as AArch64, all shared
libraries resolved, and the installed ofxMidi files matched their pre-build
checksums. Under Xvfb with Mesa llvmpipe, VOSC evaluated `0.125+0.25` to layer
alpha `0.375`.

Physical Raspberry Pi qualification requires an idle board and attached
display. Check hardware-accelerated desktop OpenGL 4.1, visible Organic
Trails output, a visible OSC cue from a separate controller, and sustained
playback on the target board and OS.
