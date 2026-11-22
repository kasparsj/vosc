# VOSC on openFrameworks 0.11.1

VOSC is a real-time 2D/3D visuals engine for OSC-driven performance and
installations. The `of-0.11.1` maintenance branch targets Apple Silicon
Macs with openFrameworks 0.11.1.

Use VOSC from SuperCollider, TidalCycles, or any OSC client to control layers,
textures, geometry, shaders, cameras, and post effects. See the
[documentation index](docs/Home.md), [runnable examples](examples/README.md),
and [changelog](CHANGELOG.md).

## Building with Xcode on macOS

Debug and Release target Apple Silicon on macOS 12.6 or later. Select Xcode
and use native Python 3.10 or later. The build uses C++17 and the macOS release
of [openFrameworks 0.11.1](https://openframeworks.cc/versions/v0.11.1/).

### Set up a local SDK and addons

Clone the maintenance branch and run setup.

```bash
git clone --branch of-0.11.1 https://github.com/kasparsj/vosc
cd vosc
python3 scripts/setup_macos.py
```

This prepares openFrameworks 0.11.1 and the pinned VOSC addons under
`.vosc-macos/`. Run the printed command to build the Debug app. For setup
directories inside the checkout, build output goes under
`~/Library/Caches/VOSC/builds/`. For setup directories outside the checkout,
build output goes in the destination's `build/` directory.
Use `--dest PATH` for another setup directory. The
[live authoring guide](docs/Live-authoring.md#start-the-renderer) shows how to
launch VOSC with these staged dependencies and connect an MCP client.

```bash
python3 scripts/setup_macos.py --sdk-only
python3 scripts/setup_macos.py --addons-only --of-root /path/to/openFrameworks
```

SDK-only prints the follow-up command for staging addons. Addon-only checks
that the supplied installation is openFrameworks 0.11.1, leaves it untouched, and
prints the build command. Setup reuses matching files on reruns. If a
destination contains a changed or unmanaged dependency tree, choose another
`--dest`.

### Build with an existing installation

To use the default make and Xcode paths, clone VOSC three levels below the
openFrameworks root.

```bash
cd apps/myApps
git clone --branch of-0.11.1 https://github.com/kasparsj/vosc
cd vosc
```

Install the dependencies listed in [addons.make](addons.make) under the same
openFrameworks installation's `addons/` directory. Use the bundled 0.11.1
`ofxOsc` and `ofxAssimpModelLoader` with their macOS libraries. The `ofxExpr`
addon needs its macOS muparser archive. Keep the optional Ultralight headers
and dylibs in the addon tree.

From the VOSC checkout, build with either command.

```bash
make Debug
make Release
```

The build checks the SDK headers and prepares a private 0.11.1 core and
compiler overlays in Xcode's build directory. Debug produces
`bin/voscDebug.app` and Release produces `bin/vosc.app`. Launch from the
checkout with `make RunDebug` or `make RunRelease`.

For the Xcode GUI, open `vosc.xcodeproj` and select `vosc Debug` or
`vosc Release`. Both schemes use openFrameworks 0.11.1 settings.

For a separate build directory, run
`python3 scripts/build_macos_profile.py --configuration Release --output /path/to/build`.
On Xcode 27.2, `make CleanDebug` can fail because `bin` contains checkout
assets. Use a fresh external `--output` directory for a clean build.
Verify the Release executable with
`lipo -archs /path/to/build/of-0.11.1/Release/products/vosc.app/Contents/MacOS/vosc`.
For a standalone run, put the runtime assets from `bin/data` next to the app
and copy `ofxDeferredShading/data/shaders/deferred` from the addon tree into
`data/shaders`. With the setup script, the addon tree is under
`.vosc-macos/addon-sdks/of-0.11.1/addons/` by default. The
[authoring build](docs/Live-authoring.md) copies these assets into its
private output directory.

For a checkout outside the SDK, put an `OF_PATH` assignment in the ignored
`Project.local.xcconfig`. Add `VOSC_ADDON_SDK` only if a different installation
supplies the addons. Use plain, unquoted absolute paths.

```xcconfig
OF_PATH = /path/to/openFrameworks
VOSC_ADDON_SDK = /path/to/addon-installation
```

For a one-off command-line build, use `make Debug OF_ROOT=/path/to/openFrameworks`
or the Python builder's `--of-root` option. Both look for addons in the selected
SDK unless you also supply `VOSC_ADDON_SDK` or `--addon-sdk`, respectively.

## Run a native scene

Pass a scene file to the renderer binary to load its setup and assets.

```sh
vosc --scene /path/to/examples/editable-scene/scene.vosc.json
vosc --scene /path/to/examples/sieve/scene.vosc.json
```

Use the executable inside `Contents/MacOS/` for a macOS app bundle. For
example, from the checkout after an in-place Debug build, run the animated
shader example at 640×480 with a 30 FPS cap.

```sh
scene="$PWD/examples/shaders/scene.vosc.json"
(cd bin && ./voscDebug.app/Contents/MacOS/voscDebug --scene "$scene" --size 640x480 --fps 30)
```

`--size` accepts dimensions from 1 to 8192 and overrides a scene's initial
window size. Without it VOSC uses the scene size or 1024×768. `--fps` accepts
1 to 1000 and defaults to 60. Vsync stays enabled. Feedback buffer sizes stored
in the scene still apply. Feedback configured to advance once per frame
advances less often per second at a lower frame cap.

Keep the manifest and its assets together when moving a folder.
The [native V2 format](docs/Native-scene-v2.md) covers built-in sources,
geometry, and media. Version 1
scene files contain `version`, `title`, `size` (width and height), and an
ordered `commands` array. Exported manifests also include `onset_gate`
(`-1`, `0`, or `1`). Hand-authored files may omit it to use `-1`.

Each command has an OSC `address` and an `args` array of `type`/`value` pairs.
Use `i` for integers, `f` for floating-point values such as `1.0`, `s` for
strings and expressions, and `asset` for a path relative to the scene file.
Vector components are consecutive typed arguments. Start with `/layers/reset`.
A successful launch prints `VOSC_SCENE_READY` after the first composed frame.

Native manifests accept PNG and JPEG images up to 4096 pixels per side and
10 million pixels per image, with a 64 MiB decoded image budget per scene.
Export and wrapped SuperCollider replay retain their 2048-pixel image limit.

Use the [scene exporter](docs/Editable-scene-contract.md) to capture a running
renderer as a native JSON scene. Add `--scd` for a SuperCollider companion.

See the [examples index](examples/README.md) for scene assets, SuperCollider
companions, and prerequisites.

## Quickstart (Local + SuperCollider)

The renderer listens on UDP port `33333` by default.

1. Build and run `vosc`.
2. Open SuperCollider and evaluate this block against a renderer you own:

```supercollider
(
~v = NetAddr("127.0.0.1", 33333);

~v.sendMsg("/onset", 0);                 // apply edits immediately
~v.sendMsg("/layers/reset", 4, "grid");    // create four fresh layers
~v.sendMsg("/tex", "*", "black_hole.jpg"); // use the bundled image
~v.sendMsg("/layer/alpha", "*", 0.8);      // set opacity
)
```

3. Interact while running:
- press `TAB` for debug inspector,
- press `f` fullscreen,
- press `r` to reload textures.

Next steps:
- browse [docs/OSC-reference.md](docs/OSC-reference.md),
- export a portable editable composition with [scene recipes](docs/Editable-scene-contract.md),
- try the [example collection](examples/README.md).

## Good Fit Workflows

vosc is a good fit when you need:
- livecoding visuals that react to musical structure (onset-gated command queueing, fast command iteration),
- a dedicated visuals node controlled remotely over OSC (laptop-to-laptop or controller-to-render-machine),
- shader/texture layering workflows for VJ sets and audiovisual performances,
- installation setups where control and render are separated.

## Testing

Run the OSC harness tests:

```bash
./scripts/run_osc_tests.sh
```

Run AddressSanitizer or UndefinedBehaviorSanitizer builds:

```bash
SANITIZER=address ./scripts/run_osc_tests.sh
SANITIZER=undefined ./scripts/run_osc_tests.sh
```

For the private 0.11.1 core, instrumented app build, and OSC receiver
shutdown checks, see the [shutdown procedure](docs/shutdown-lifecycle.md#validation).

## Other build targets

See the [main branch README](https://github.com/kasparsj/vosc/blob/main/README.md)
for openFrameworks 0.12.1 and the minimal Linux build.

## Docs Sync

Generate canonical OSC docs from parser/handlers + metadata:

```bash
python3 scripts/generate_osc_reference.py
```

Validate internal markdown links and anchors:

```bash
bash scripts/check_md_links.sh
```

PRs should include regenerated `docs/OSC-reference.md` whenever command surface changes.

## Contributors

The engine is built on openFrameworks and uses the following addons:

- ofxAssimpModelLoader
- ofxAutoReloadedShader
- ofxColorTheory
- ofxDeferredShading
- ofxEasing
- ofxExpr
- ofxHPVPlayer
- ofxImGui
- ofxLooper
- ofxMidi
- ofxOsc
- ofxPostProcessing
- ofxShadertoy
- ofxTidalCycles
- ofxUltralight (optional)
