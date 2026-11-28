# Run GLSL and Hydra in the WebGPU draft preview

Pair VOSC Editor with a Render Host that has Dawn and the optional shader
compilers. Open a GLSL fragment, WGSL fragment, or Hydra patch and press
**Run**. The Inspector names the backend after the first frame appears.
Supported GLSL and single-pass Hydra reuse the same WebGPU preview window
and device as WGSL. A rejected Run leaves the previous shader drawing.

A shader outside the WebGPU profile can run on the OpenGL path while that
preview is active. If WebGPU is active, close the preview window before
running that shader. Older hosts keep their existing OpenGL behavior.

Save working edits and the last accepted source in a `.vosc-draft` file. Use
**Restore preview** to run the accepted source after reopening the file.
For translated GLSL or Hydra, the host checks the saved compiler identity
and compiled WGSL digest. Press **Run** to accept a fresh result if either
has changed. Standalone replay export remains available for OpenGL drafts.

## Translation profile

The supported GLSL profile is desktop GLSL 330 with one fragment entry point
and one `out vec4`. VOSC supplies `time`, `resolution`, and
`gl_FragCoord.xy` with the GLSL lower-left origin. Float and vector uniforms
support scalar and vector numeric defaults. VOSC fills omitted defaults with
zero. Each uniform uses a 16-byte lane in the second uniform buffer.
[Native WGSL interface](WebGPU-M2a.md#shader-interface)

Textures, feedback, extra passes, and existing editable mappings use the
OpenGL path. The WebGPU Inspector shows the accepted backend and frame.
Editing WebGPU uniform values and OSC mappings follows in M2b.2.

## Package the compiler tools

Build the renderer with the [pinned Dawn SDK](WebGPU-M1.md#dawn-sdk).
Build glslang 15.1.0 for your target platform, or use the pinned Ubuntu
24.04 x86-64 glslang package described in the M1 translation probe.
On Linux, use the `tint` executable from the pinned Dawn SDK. For Apple
Silicon macOS, build Tint from the same Dawn source revision with the
SPIR-V reader enabled. The macOS SDK binary omits that reader.

In a clean Dawn source checkout at
`e4775a0d00343d02d32daecd45e1eb683224214b`, run the following commands.
The source can remain outside the VOSC repository.

```bash
python3 "$dawn_source/tools/fetch_dawn_dependencies.py" --directory "$dawn_source" --shallow
cmake -S "$dawn_source" -B "$tint_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DBUILD_SHARED_LIBS=OFF \
  -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF -DDAWN_BUILD_PROTOBUF=OFF \
  -DDAWN_USE_GLFW=OFF -DDAWN_BUILD_MONOLITHIC_LIBRARY=OFF \
  -DDAWN_ENABLE_METAL=OFF -DDAWN_ENABLE_VULKAN=OFF \
  -DDAWN_ENABLE_OPENGLES=OFF -DDAWN_ENABLE_DESKTOP_GL=OFF -DDAWN_ENABLE_NULL=OFF \
  -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_CMD_TOOLS=ON \
  -DTINT_BUILD_SPV_READER=ON -DTINT_BUILD_WGSL_WRITER=ON
cmake --build "$tint_build" --target tint_cmd_tint_cmd
```

Set `tint` to the resulting executable. Stage both tools with the same
script on Linux and Apple Silicon macOS.

```bash
python3 desktop/authoring/scripts/stage_webgpu_tools.py prepare \
  --glslang "$glslang" --tint "$tint" --output "$scratch/webgpu-tools"
```

Pass `--webgpu-tools "$scratch/webgpu-tools"` to the Linux Render Host
stager or the macOS packager. The package includes the tool versions,
binary hashes, and license notices. VOSC launches the packaged compiler
paths. A package with missing or invalid compiler tools can still run
native WGSL when Dawn is available.

## Verify on hardware

Run the [M1 and M2a smoke checks](WebGPU-M2a.md#verify-the-packaged-renderer).
Build with `VOSC_RECOVERY_TESTING=1` for pixel verification. In a desktop
session, start a private preview at 640 × 480. Use a private writable
`draft_data` directory with the packaged renderer shader assets. Set
`shader_tools` to the absolute path of the installed `webgpu-tools`
directory. On Linux, set `LD_LIBRARY_PATH` to the packaged runtime
libraries before starting the renderer.

```bash
VOSC_DRAFT_SHADER_TOOLS="$shader_tools" VOSC_CONTROL_SOCKET="$draft_socket" \
  "$renderer" --draft-data-root "$draft_data" --draft-preview --size 640x480 --fps 30
```

In a second terminal, run the verifier.

```bash
node desktop/authoring/tests/emit-webgpu-m2b-fixture.mjs "$scratch/animated.frag"
python3 scripts/authoring/verify_webgpu_m2b.py \
  --socket "$draft_socket" --hydra "$scratch/animated.frag" --output "$scratch/parity"
```

The emitter uses the Editor's staged pinned Triode build. The verifier
requires Pillow. It records the device and process, rendered revisions,
PNG readbacks, and rejection receipts. It compares interior pixels at
0.25 s and checks the coordinate and uniform defaults with two deliberately
different reference shaders.

## Hardware results

The 640 × 480 readbacks used the same private socket, renderer process,
window, and WebGPU device across WGSL, GLSL, Hydra, and WGSL. Both
platforms passed the negative controls, rejected-run checks, and the
original M1 and M2a smokes.

| Backend and device | Glow Ribbons max RGBA difference | Hydra max RGBA difference |
| --- | --- | --- |
| Vulkan on NVIDIA GeForce GTX 1080, Ubuntu 24.04 | 0, 0, 0, 0 | 1, 1, 1, 0 |
| Metal on Apple M3 Pro, macOS arm64 | 0, 0, 0, 0 | 1, 1, 1, 0 |

The Linux renderer used Release with `-fno-tree-dce` to work around the
local GCC build failure. The macOS renderer used Xcode Debug with the
package and tools ad hoc signed. Both builds enabled recovery testing.
