# Run WGSL in VOSC Editor

Create a **WGSL fragment** from **New**, or use **Open…** to open a
`.wgsl` file. Connect to a paired Render Host with a Dawn-enabled VOSC
renderer and press **Run**. The Inspector shows **WebGPU** and the first
frame after the shader presents. A failed Run keeps the previous preview
while its device is healthy.

Save the working source as a `.vosc-draft` file. If you save after editing
a working shader, the draft also keeps the last accepted source. Use
**Restore preview** to run that accepted source when you reopen the draft.
Standalone replay export is only available for GLSL and Hydra drafts.

The [optional GLSL and Hydra compiler package](WebGPU-M2b.md) lets
supported shaders use the same WebGPU preview as WGSL. With a WGSL-only
host, close the WebGPU preview window before running GLSL or Hydra.
Closing the window ends the private renderer session.

## Shader interface

VOSC supplies a full-screen triangle. Write the fragment entry point as
follows. The UV origin is the top left and both coordinates run from 0 to 1.

```wgsl
@fragment fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {
    return vec4f(uv.x, uv.y, 0.6, 1.0);
}
```

A shader can also declare this optional uniform buffer.

```wgsl
struct Renderer {
    resolution: vec2f,
    time: f32,
}

@group(0) @binding(0) var<uniform> renderer: Renderer;
```

VOSC uses a fragment-visible layout with a 16-byte buffer. The framebuffer
resolution in pixels starts at byte 0. Elapsed preview time in seconds
starts at byte 8. The final four bytes are padding. WGSL previews currently
support this renderer-owned buffer and one fragment pass. Inspector
bindings, OSC slots, textures, and feedback are outside this profile.

## Verify the packaged renderer

Follow the [Dawn SDK and build instructions](WebGPU-M1.md) for Linux x86-64
or Apple Silicon macOS. Use an active desktop session. Keep the original M1
smoke as a check of the experimental OpenGL → WebGPU → OpenGL path.

On Linux, use the existing Render Host staging command with your built
renderer. From the repository root, run the native transaction check with
the staged host directory in `host_install` and a writable absolute evidence
directory in `scratch`.

```bash
desktop/authoring/node_modules/.bin/esbuild \
  desktop/authoring/tests/verify-webgpu-m2a.ts --bundle --platform=node \
  --target=node22 --format=esm --outfile="$scratch/verify-webgpu-m2a.mjs"
node "$scratch/verify-webgpu-m2a.mjs" "$host_install" "$scratch/m2a-vulkan.json"
```

For macOS, stage the renderer and test host bundle as in the
[M1 macOS smoke](WebGPU-M1.md#macos-build). Compile the verifier with the
Swift host transaction code.

```bash
xcrun swiftc -o "$host/MacOS/verify-webgpu-m2a" \
  desktop/authoring/host/{Wire,HostIdentity,DraftRenderer,Draft}.swift \
  desktop/authoring/tests/webgpu-m2a/main.swift
"$host/MacOS/verify-webgpu-m2a" \
  "$PWD/desktop/authoring/tests/fixtures/webgpu" "$scratch/m2a-metal.json"
```

Both verifiers exercise the normal native prepare, commit, cancel, and fence
operations over the private socket. They check quadrant colors, framebuffer
resolution, elapsed time, and that replacing a shader keeps the same process
and WebGPU window and device. They also call the host's normal
`run.prepare` and `run.commit` methods. With a Dawn-free renderer, both
verifiers check that the host rejects WGSL and can still run GLSL.

To test rollback after preparing and drawing a valid candidate, build the
renderer with `VOSC_RECOVERY_TESTING=1` and pass `--recovery` to the verifier.
On Linux, add `PROJECT_CFLAGS=-DVOSC_RECOVERY_TESTING=1` to the inner Make
command in `VOSC_PROFILE_MAKE`. On macOS, pass
`'GCC_PREPROCESSOR_DEFINITIONS=$(inherited) VOSC_RECOVERY_TESTING=1'` to the
Xcode build. The injection fails the first-frame path before displaying the
candidate. The verifier checks that the previous revision can still present.
