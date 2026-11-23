# Changelog

## v0.2.0

### Renderer interface

- Added a first-run Welcome and a native operator overlay. Press **O** to open it and inspect live layers, shader uniforms, and diagnostics. [Operator reference](docs/Render-machine-ui.md)
- Browse shared shaders, images, and models under **Browse → Assets**. **Browse → Examples** discovers installed example folders and loads supported scenes into the current renderer window. [Example guide](examples/README.md)

### Scenes and OSC

- Open portable scene manifests with `vosc --scene`. Export now writes `scene.vosc.json` and its assets by default. Pass `--scd` to also generate editable SuperCollider source. [Scene launch](README.md#run-a-native-scene) · [Scene export](mcp/README.md#editable-scene-folders)
- Consolidated the shipped examples into portable scene folders with SuperCollider companions where applicable. Some scenes require a local video clip, camera, or optional build capability. [Example guide](examples/README.md)
- Resolve texture aliases when their queued OSC commands are dispatched. The direct draw path also checks the window sample count before skipping its intermediate render target.

### Build and distribution

- The macOS build now requires openFrameworks 0.12.1 and uses its bundled ofxOsc and ofxAssimpModelLoader. The build retains OSC receiver recovery and existing model framing. [macOS build instructions](README.md#building-with-xcode-on-macos)
- The minimal Linux build stages the openFrameworks 0.12.1 SDK inputs and addons in an isolated project. Linux ARM64 compilation and runtime qualification are pending. [Linux build instructions](README.md#minimal-linux-build-and-render-settings)

## v0.1.0

### Breaking changes

- **macOS builds support openFrameworks 0.11.1 and 0.12.1.** Run `python3 scripts/setup_macos.py` to obtain the SDK and pinned addons. The command-line builders select the SDK version automatically. An ignored `Project.local.xcconfig` supports other layouts. [Build instructions](README.md#building-with-xcode-on-macos)
- **Configure OSC input slots before sending samples or calibration.** Send `/input <target>` for an OSC-fed slot, then send `/input/data` or `/input/set`. Both ignore unknown slots. `/input/set` requires one finite OSC float for `maxAmp` or `maxLoud`. Wildcard `/input` setup requires explicit device, channel count, and buffer size. [Input commands](docs/Setup-commands.md#input)
- **Layer and material selectors reject previously coerced values.** Use a nonnegative OSC int32, a string containing only decimal digits within int32 range, or one of the wildcards `*`, `x`, and `a`. Strings such as `1fx` no longer select layer 1 for `/layer*` or `/mat*`. [Layer commands](docs/Layer-commands.md) · [Material commands](docs/Material-commands.md)
- **Layer layout commands reject extra arguments and invalid layout values.** Use `stack`, `grid`, `col`, `row`, or their numeric enum values 1–4. `/layers/layout` takes exactly one layout argument. `/layers` and `/layers/reset` take an optional count and layout. [Setup commands](docs/Setup-commands.md#layers)
- **Layer shader vector arrays must match the active uniform’s arity.** Trim oversized JSON arrays for reflected `vec2/3/4` and `ivec2/3/4` uniforms. Existing `vec4` color bindings and live `ivec4`-to-`vec4` transitions also accept three RGB values and supply alpha 1. [Shader commands](docs/Shader-commands.md#shadervar-and-shaderuniform)

### Live authoring and feedback

- Added an opt-in local engine API and typed MCP adapter for scene inspection, shader publication, layer and uniform edits, diagnostics, and composed PNG previews. `VOSC_OSC_PORT` lets a second renderer receive OSC on a separate port. [Live authoring guide](docs/Live-authoring.md) · [Engine API](docs/Engine-api.md)
- Added frame cadence, CPU submission time, and GPU timing where supported. Protected direct-source shader trials use these measurements to evaluate a candidate before explicit acceptance. Cancel, lease expiry, or budget rejection restores the working shader. Explicit shader replacement also keeps the working program when compilation or linking fails. [Performance telemetry](docs/Performance-telemetry.md) · [Shader trials](docs/Shader-trials.md)
- Added protected shader trials for the default feedback texture in the supported two-layer 800×600 scene. Configured OSC input samples continue during evaluation, previews identify the trial and candidate program, and cancellation resumes the previous feedback buffers. Acceptance keeps the candidate's live history and releases the previous program and buffers. [MCP feedback trial usage](mcp/README.md#tools-and-lifecycle)

### Editable scenes and OSC

- Added export of portable scene folders containing editable SuperCollider source and assets. Supported recipes preserve layer settings, shader source, expressions, images, and input calibration. Replay clears obsolete scene-owned resources and queued commands before rebuilding the composition. [Editable scene contract](docs/Editable-scene-contract.md)
- Added export and replay for a single supported default ping-pong shader texture. Replay starts both feedback buffers from white. Recipes with feedback require VisualsQuark feedback replay API 1. [Feedback composition](docs/Feedback-composition-contract.md) · [Editable scene folders](mcp/README.md#editable-scene-folders)
- Added layout-name shorthand for `/layers` and `/layers/reset`, amplitude-only `/input/data`, three-component JSON arrays for `/layer/pos`, and reflected float and integer vector bindings for layer shaders. Added `/layer/blend`, `/layer/speed`, and shader texture size, speed, and settings commands. [OSC reference](docs/OSC-reference.md)

### Build, reliability, and examples

- The macOS build creates a private openFrameworks core and compatibility overlays in its build directory. Fixed integer color construction, improved scene resource cleanup, and made shutdown join the OSC receiver before releasing renderer state. [Build instructions](README.md#building-with-xcode-on-macos) · [OSC receiver](vendor/ofxOsc/README.md)
- Refreshed SuperCollider examples for the current OSC commands and added an editable-scene example and the Organic Trails shader. [SuperCollider examples](examples/README.md) · [Organic Trails](examples/organic-trails/README.md)
