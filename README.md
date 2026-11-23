# vosc

**This is work in progress!**

vosc is a real-time 2D/3D visuals engine for OSC-driven performance and installations.

At a high level, vosc gives you:
- a render runtime (layers, textures, geometry, shaders, camera, post effects),
- an OSC command surface to control that runtime live,
- a workflow where your music/livecoding environment drives visuals over the network.

It is mainly used from SuperCollider / TidalCycles, but any OSC client can drive it.

See [docs/Home.md](docs/Home.md) for full docs, [Examples](examples/README.md) for runnable sketches, and the [changelog](CHANGELOG.md) for recent changes.

## Good Fit Workflows

vosc is a good fit when you need:
- livecoding visuals that react to musical structure (onset-gated command queueing, fast command iteration),
- a dedicated visuals node controlled remotely over OSC (laptop-to-laptop or controller-to-render-machine),
- shader/texture layering workflows for VJ sets and audiovisual performances,
- installation setups where control and render are separated.

Compact hardware workflow:
- vosc can be used in Raspberry Pi-class workflows as an OSC-controlled visual node or as part of a split setup.
- In practice, feature availability/performance depends on GPU drivers/OpenGL support and enabled addons.
- Heavier shader/post pipelines are better suited to desktop GPUs.

### Minimal Linux build and render settings

Use the Linux openFrameworks 0.12.1 SDK with the retained addons from
`addons.make`. Linux ARM64 compilation and runtime qualification are pending.

```sh
make Release VOSC_PROFILE=minimal OF_ROOT=/absolute/path/to/openFrameworks
```

The profile skips ofxDeferredShading, ofxHPVPlayer, and ofxUltralight.
It stages its executable and runtime assets in a fresh `.vosc-linux/minimal-*`
directory and prints the run location. It includes forward post-processing,
OSC, image and shader layers, and shader feedback. The minimal profile uses
`VOSC_OPERATOR_UI=0`, excluding the RmlUi operator overlay and its input controls. Terminal and retained engine
diagnostics remain available.
Put any regular video files you need in the staged `bin/data/videos` directory.

From the printed run location, this command opens a scene at 1280×720 with a
30 FPS cap.

```sh
./vosc --scene /absolute/path/to/scene.vosc.json --size 1280x720 --fps 30
```

`--size` accepts dimensions from 1 to 8192 and overrides a scene's initial
window size. Without it VOSC uses the scene size or 1024×768. `--fps` accepts
1 to 1000 and defaults to 60. Vsync stays enabled. Feedback buffer sizes stored
in the scene still apply. Feedback configured to advance once per frame
advances less often per second at a lower frame cap.

The minimal profile uses the existing desktop OpenGL 4.1 renderer. Verify
hardware-accelerated context creation and the intended shaders on the target
board and OS before relying on it in an installation.

### Desktop Linux build

The default Linux Make build uses a desktop OpenGL 4.1 context and RmlUi for
the operator overlay. It needs the complete `addons.make` set, CMake 3.16 or
later, curl, and the FreeType development package. The first UI-enabled build
fetches and verifies its pinned RmlUi source archive. Use
`make Release VOSC_OPERATOR_UI=0 OF_ROOT=/absolute/path/to/openFrameworks` to
build the desktop profile with the operator overlay excluded. Use the minimal profile above to also reduce the addon set.

## Building with Xcode on macOS

Use the macOS release of openFrameworks [0.12.1](https://github.com/openframeworks/openFrameworks/releases/tag/0.12.1).
Debug targets Apple Silicon. Release targets Apple Silicon and Intel
(`arm64` and `x86_64`) in a single Universal app. The build targets macOS 12.6
or later and uses Python 3.10 or later. UI-enabled builds also use CMake 3.16
or later, curl, and the openFrameworks SDK's bundled FreeType.
For openFrameworks 0.11.1, use the
[maintenance branch](https://github.com/kasparsj/vosc/tree/of-0.11.1).

The desktop build enables `VOSC_OPERATOR_UI=1`. Its first build fetches the
pinned RmlUi 6.3 sources and verifies their archive digest. It compiles RmlUi
core and the upstream OpenGL 3 backend, and embeds the pinned Lato Latin
Regular and Bold faces in the executable. Build with
`make Debug VOSC_OPERATOR_UI=0` or the Xcode setting `VOSC_OPERATOR_UI=0`
to omit those inputs and the operator overlay. Rendering,
OSC, and terminal and retained diagnostic logging remain available.

### Set up a local SDK and addons

From a VOSC checkout on Apple Silicon, with native Python 3.10 or later
and Xcode selected, run:

```bash
python3 scripts/setup_macos.py
```

This prepares openFrameworks 0.12.1 and the pinned VOSC addons under
`.vosc-macos/`. It stages ofxAssimpModelLoader and ofxOsc from the same
verified 0.12.1 macOS archive as the core. Run the printed command to build the
Debug app. For setup directories inside the checkout, build output goes under
`~/Library/Caches/VOSC/builds/`. For setup directories outside the checkout,
build output goes in the destination's `build/` directory.
Use `--dest PATH` to choose a setup directory. For MCP, follow the
[live authoring setup](docs/Live-authoring.md#start-the-renderer).

```bash
python3 scripts/setup_macos.py --sdk-only
python3 scripts/setup_macos.py --addons-only --of-root /path/to/openFrameworks
```

SDK-only prints the follow-up command for staging addons. Addon-only verifies
openFrameworks 0.12.1, leaves the supplied installation untouched, and prints
the build command. Setup reuses matching files on reruns. If a
destination contains a changed or unmanaged dependency tree, choose another
`--dest`. Use a fresh destination for managed addon trees created before this
migration.

### Build with an existing installation

To use the default make and Xcode paths, clone VOSC three levels below the
openFrameworks root.

```bash
cd apps/myApps
git clone https://github.com/kasparsj/vosc
cd vosc
```

Install the dependencies listed in [addons.make](addons.make) under the same
openFrameworks installation's `addons/` directory. Use the bundled 0.12.1
`ofxOsc` and `ofxAssimpModelLoader`, including Assimp's macOS xcframework.
Use libraries with both architecture slices for a Universal build. The `ofxExpr`
addon needs its macOS muparser archive. Keep the optional Ultralight headers and
dylibs for Apple Silicon Debug. Universal Release excludes Ultralight.

From the VOSC checkout, build with either command.

```bash
make Debug
make Release
```

The build verifies the SDK version and uses C++23. It prepares a private core
and compiler overlays in Xcode's build directory. Debug produces
`bin/voscDebug.app` and Release produces `bin/vosc.app`. Use `make RunDebug`
or `make RunRelease` from the checkout to launch it.

For the Xcode GUI, open `vosc.xcodeproj` and select `vosc Debug` or
`vosc Release`. Both schemes use openFrameworks 0.12.1 settings.

For a separate build directory, run
`python3 scripts/build_macos_profile.py --configuration Release --output /path/to/build`.
Verify the Release executable with
`lipo -archs /path/to/build/of-0.12.1/Release/products/vosc.app/Contents/MacOS/vosc`.
The standalone build stages a `data/` directory beside the app in `products/`.
Keep both together when distributing the app. The runtime's deferred shaders
are staged from the configured addon SDK.

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

### Shared assets and local media

Edit shared shaders, images, and models in [assets/](assets/). Build output uses
copies in `data/` beside the app or Linux executable. Each portable example
keeps its assets beside its scene manifest.

Refresh assets in an existing macOS build without recompiling. Set the addon
SDK to the path used by the build. Use `bin/data` for an in-place app or
the build's `products/data` for an isolated app.

```sh
python3 scripts/stage_assets.py bin/data --addon-sdk /path/to/addon-sdk
```

Keep personal clips in ignored `media/videos/` or in an external media folder.
Pass external clips by absolute path to scenes that accept a video input.
For filename lookup or random selection, add `--include-videos` to the refresh
command or copy the needed clips into the staged `data/videos/` directory.
The authoring build includes clips from `media/videos/` by default.
Downloaded Shadertoy files stay in writable runtime `data/shaders/shadertoy/`.
Use a fresh output without private clips or downloads when assembling a release.
For direct downloads on macOS, the DMG contains only `vosc.app`. Open it or
copy it to Applications. On first launch VOSC prepares editable shaders,
assets and examples in `~/Library/Application Support/VOSC/data/`. Press
**O**, open **Browse**, and choose **Open Data Folder** to edit them in Finder.
See the [macOS data and migration guide](docs/Macos-dmg-smoke.md#editable-data)
and the [release qualification guide](docs/Macos-dmg-smoke.md#assemble-the-release).

## Run a native scene

Pass a scene file to the renderer binary to load its setup and assets.

```sh
vosc --scene /path/to/examples/editable-scene/scene.vosc.json
vosc --scene /path/to/examples/sieve/scene.vosc.json
```

Use the actual executable inside `Contents/MacOS/` for a macOS app bundle.
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
- ofxLooper
- ofxMidi
- ofxOsc
- ofxPostProcessing
- ofxShadertoy
- ofxTidalCycles
- ofxUltralight (optional)

The desktop diagnostic overlay uses [RmlUi 6.3](https://github.com/mikke89/RmlUi/tree/6.3)
and its Lato Latin sample fonts. Their license texts are in the pinned
[RmlUi archive](https://github.com/mikke89/RmlUi/blob/6.3/LICENSE.txt) and
[font sample directory](https://github.com/mikke89/RmlUi/blob/6.3/Samples/assets/LICENSE.txt).
