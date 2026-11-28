# Native WebGPU feasibility spike

A VOSC build linked with Dawn can switch the private draft preview from
OpenGL to a GLFW WebGPU surface and back in the same process. It uses the
existing Render Host launcher and Unix socket. A normal launch still opens
the OpenGL renderer.

The private socket offers two experimental operations in an opt-in build.

- `draft_m1_status` reports the active backend, adapter, process ID, frame
  counters, and each window's visibility.
- `draft_m1_switch` accepts `{"backend":"webgpu","source":"…"}` or
  `{"backend":"opengl"}`. WebGPU source provides
  `@fragment fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f`.
  The built-in vertex shader supplies normalized coordinates with the origin
  at the top left. The first WebGPU frame receipt includes four RGBA samples
  from the same texture copied to the native surface.

Dawn renders into a texture before taking over the preview. On X11 it also
presents once while the new surface is hidden. On macOS the completed
offscreen frame gates showing the new window, then the surface presents
while the OpenGL window remains visible. Only a successful first WebGPU
presentation hides the previous output. A failed candidate leaves OpenGL
visible and its shader state intact. The spike waits for Dawn on the OF
window thread, so candidate work can pause OpenGL animation before it
resumes. The private preview uses a fixed-size surface and a fragment
shader with UV input. Linux uses X11.

## Dawn SDK

The fetch script verifies the SHA-256 of the upstream static Release
archive for Linux x86-64 or macOS. The archive version is
`v20261127.173033`, revision `e4775a0d00343d02d32daecd45e1eb683224214b`.
It includes the matching Tint CLI. Omit the Dawn settings for an ordinary
GL-only build.

```bash
scratch="$PWD/.vosc-linux/webgpu-m1"
bash scripts/fetch_webgpu_m1_dawn.sh "$scratch/dawn"
```

## Linux build and smoke

Use a prepared openFrameworks 0.12.1 SDK with the Linux build dependencies
and the retained addons installed. The x86-64 release artifact
`of_v0.12.1_linux64_gcc6_release.tar.gz` has SHA-256
`d6c1dcab777665b2aa63e5e3d9122cc116f096b3421db3493f795a621b399c63`.
See the [Linux dependency and addon setup](Linux-arm64-minimal.md). The
build and smoke commands below use x86-64 and an X11 display with a
Vulkan-capable adapter.

```bash
make Release VOSC_PROFILE=minimal VOSC_OPERATOR_UI=0 OF_ROOT="$of_sdk" \
  VOSC_PROFILE_MAKE='make JOBS=-j6' VOSC_DAWN_SDK="$scratch/dawn" \
  | tee "$scratch/experimental-build.log"
stage=$(sed -n 's/^Building VOSC minimal in //p' "$scratch/experimental-build.log")
```

Stage the renderer with the existing Linux Render Host packager. The host
uses only the packaged `runtime-libs` for extra shared libraries. Set
`runtime_libs` to an absolute directory containing the renderer's shared
libraries that the target OS does not provide, including FMOD if needed.
Use the full source and SDK Git revisions that correspond to your build.
Install the Editor's existing Node dependencies for packaging.

```bash
node desktop/authoring/scripts/stage_linux_host.mjs \
  --output "$scratch/host" --renderer-bin "$stage/bin/vosc" \
  --source-revision "$(git rev-parse HEAD)" \
  --sdk-revision 1019e6a783a309dac8d8d61cb990967bec5ce8f9 \
  --runtime-libs "$runtime_libs"
desktop/authoring/node_modules/.bin/esbuild \
  desktop/authoring/tests/verify-webgpu-m1.ts --bundle --platform=node \
  --target=node22 --format=esm --outfile="$scratch/verify-webgpu-m1.mjs"
node "$scratch/verify-webgpu-m1.mjs" "$scratch/host" \
  desktop/authoring/tests/fixtures/webgpu/m1-quadrants.wgsl "$scratch/frames.json"
```

The smoke starts the real Linux `NativeRenderer`, publishes a GLSL
baseline, and uses its original control socket for the complete sequence.
It checks that invalid WGSL returns a failed receipt, the same GL shader
continues yielding identical PNGs, a subsequent Vulkan frame matches all
four RGBA samples, Vulkan presents more frames, and returning to GL yields
the original capture again. `frames.json` records the receipts.

## macOS build

Use the Xcode wrapper with the SDK matching the machine's architecture.

```bash
python3 scripts/build_macos_profile.py --configuration Release \
  --of-root "$of_sdk" --output "$scratch/mac-build" -- \
  VOSC_WEBGPU_M1=1 "VOSC_DAWN_SDK=$scratch/dawn" \
  VOSC_OPERATOR_UI=0 ARCHS=arm64 CODE_SIGNING_ALLOWED=NO
```

The macOS smoke uses the real Swift `DraftRenderer` with a thin verifier
packaged beside the renderer app. Run it from a logged-in desktop session.

```bash
mac_stage="$scratch/mac-smoke"
app="$mac_stage/vosc.app"
host="$mac_stage/VOSC Render Host.app/Contents"
mkdir -p "$host/MacOS"
ditto "$scratch/mac-build/of-0.12.1/Release/products/vosc.app" "$app"
python3 scripts/stage_assets.py "$app/Contents/Resources/data" --addon-sdk "$of_sdk"
cp desktop/authoring/tests/webgpu-m1/Info.plist "$host/Info.plist"
xcrun swiftc -o "$host/MacOS/verify-webgpu-m1" \
  desktop/authoring/host/{Wire,HostIdentity,DraftRenderer}.swift \
  desktop/authoring/tests/webgpu-m1/main.swift
"$host/MacOS/verify-webgpu-m1" \
  desktop/authoring/tests/fixtures/webgpu/m1-quadrants.wgsl "$scratch/metal-frames.json"
```

For a Debug build, stage `voscDebug.app` as `vosc.app`, rename its
`Contents/MacOS/voscDebug` to `Contents/MacOS/vosc`, and update
`CFBundleExecutable` to `vosc` in its `Contents/Info.plist`.

## GLSL and Hydra translation probe

Run with glslangValidator and the Tint CLI in the pinned Dawn SDK.

```bash
python3 scripts/probe_webgpu_m1_translation.py \
  --glslang "$(command -v glslangValidator)" --tint "$scratch/dawn/bin/tint" \
  --output "$scratch/translation"
```

The inputs are the shipped [glow ribbons fragment](../examples/shaders/assets/glow-ribbons.frag)
and the recorded generated single-pass GLSL in the accepted
[legacy Hydra draft](../desktop/authoring/tests/fixtures/hydra/legacy-m4.vosc-draft).
The Hydra probe uses that fixture because the current Triode compiler
artifact was unavailable on the Linux test machine. Both raw desktop GLSL
inputs fail glslang 15.1.0 Vulkan compilation because non-opaque
uniforms are outside a block. Converting to GLSL 450, moving those uniforms
to a `std140` block at set 0, binding 0, and assigning output location 0
lets glslang emit SPIR-V. The conversion also moves fragment coordinates
back to the lower-left origin. A host would need to supply the original
uniform defaults, `glow=1.0` and `u_hydra_mix=0.5`.

Pinned Tint emitted and revalidated 1,207 bytes of WGSL for glow ribbons
and 3,461 bytes for Hydra. The probe checks translation and validation.
Rendering translated WGSL and comparing its pixels requires a host that
supplies the translated bindings.
