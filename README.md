# vosc

**This is work in progress!**

vosc is a real-time 2D/3D visuals engine for OSC-driven performance and installations.

At a high level, vosc gives you:
- a render runtime (layers, textures, geometry, shaders, camera, post effects),
- an OSC command surface to control that runtime live,
- a workflow where your music/livecoding environment drives visuals over the network.

It is mainly used from SuperCollider / TidalCycles, but any OSC client can drive it.

See [docs/Home.md](docs/Home.md) for full docs, [SuperCollider examples](SuperCollider/README.md) for runnable sketches, and the [changelog](CHANGELOG.md) for recent changes.

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

## Building with Xcode (Apple Silicon)

Use the macOS release of openFrameworks [0.11.1](https://openframeworks.cc/versions/v0.11.1/)
or [0.12.1](https://github.com/openframeworks/openFrameworks/releases/tag/0.12.1).
The Xcode build targets Apple Silicon and macOS 12.6 or later and uses Python 3.10 or later.

### Set up a local SDK and addons

From a VOSC checkout on Apple Silicon, with native Python 3.10 or later
and Xcode selected, run:

```bash
python3 scripts/setup_macos.py
```

This prepares openFrameworks 0.12.1 and the pinned VOSC addons under
`.vosc-macos/`. Run the printed command to build the Debug app. For setup
directories inside the checkout, build output goes under
`~/Library/Caches/VOSC/builds/`. For setup directories outside the checkout,
build output goes in the destination's `build/` directory.
Use `--version 0.11.1` for the older SDK or `--dest PATH` for another setup
directory. For MCP, follow the [live authoring setup](docs/Live-authoring.md#start-the-renderer).

```bash
python3 scripts/setup_macos.py --sdk-only --version 0.11.1
python3 scripts/setup_macos.py --addons-only --of-root /path/to/openFrameworks
```

SDK-only prints the follow-up command for staging addons. Addon-only reads
the SDK version from the supplied installation, leaves it untouched, and
prints the build command. Setup reuses matching files on reruns. If a
destination contains a changed or unmanaged dependency tree, choose another
`--dest`.

### Build with an existing installation

To use the default make and Xcode paths, clone VOSC three levels below the
openFrameworks root.

```bash
cd apps/myApps
git clone https://github.com/kasparsj/vosc
cd vosc
```

Install the dependencies listed in [addons.make](addons.make) under the same
openFrameworks installation's `addons/` directory. The project uses the
0.11.1 layout of `ofxOsc` and `ofxAssimpModelLoader` for both builds. For 0.12.1,
use those addon sources from the 0.11.1 release, with their macOS libraries.
Use Apple Silicon builds of the libraries used by enabled addons. In particular,
`ofxExpr` needs its macOS muparser archive. Keep the `ofxUltralight` headers and
dylibs in the addon tree for the Xcode project.

From the VOSC checkout, build with either command.

```bash
make Debug
make Release
```

The build reads the SDK version from its headers and selects C++17 for 0.11.1
or C++23 for 0.12.1. It prepares a private core and compiler overlays in Xcode's
build directory. Debug produces `bin/voscDebug.app` and Release produces
`bin/vosc.app`. Use `make RunDebug` or `make RunRelease` from the checkout to launch it.

For the Xcode GUI, open `vosc.xcodeproj` and select `vosc Debug` or
`vosc Release`. The project uses 0.12.1 settings by default. For 0.11.1, add
this line to the ignored `Project.local.xcconfig` at the VOSC root before
opening the project.

```xcconfig
#include "scripts/OpenFrameworks011.xcconfig"
```

For a separate build directory, run
`python3 scripts/build_macos_profile.py --configuration Release --output /path/to/build`.
For a standalone run, put the runtime assets from `bin/data` next to the app
and copy `ofxDeferredShading/data/shaders/deferred` from the addon tree into
`data/shaders`. With the setup script, the addon tree is under
`.vosc-macos/addon-sdks/of-VERSION/addons/` by default. The
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
- try the [SuperCollider example collection](SuperCollider/README.md) and the [Organic Trails shader](examples/organic-trails/README.md).

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
- ofxImGui
- ofxLooper
- ofxMidi
- ofxOsc
- ofxPostProcessing
- ofxShadertoy
- ofxTidalCycles
- ofxUltralight (optional)
