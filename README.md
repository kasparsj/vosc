# vosc

**This is work in progress!**

vosc is a real-time 2D/3D visuals engine for OSC-driven performance and installations.

At a high level, vosc gives you:
- a render runtime (layers, textures, geometry, shaders, camera, post effects),
- an OSC command surface to control that runtime live,
- a workflow where your music/livecoding environment drives visuals over the network.

It is mainly used from SuperCollider / TidalCycles, but any OSC client can drive it.

See [docs/Home.md](docs/Home.md) for full docs and [SuperCollider](SuperCollider) for more scripts.

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

## Quickstart (Local + SuperCollider)

Default OSC receive port in this app is `33333` (`src/ofApp.h`).

1. Build and run `vosc`.
2. Open SuperCollider and run:

```supercollider
(
~v = NetAddr("127.0.0.1", 33333);

~v.sendMsg("/layers", 4, "grid");         // 4 layers in grid layout
~v.sendMsg("/tex/choose", "*", "image");  // random image source on all layers
~v.sendMsg("/layer/alpha", "*", 0.8);     // set opacity
~v.sendMsg("/cam", "easy");               // enable easy camera
~v.sendMsg("/shading", "deferred");       // optional: switch shading mode
~v.sendMsg("/shading/passes", "bloom", "fxaa");
)
```

3. Interact while running:
- press `TAB` for debug inspector,
- press `f` fullscreen,
- press `r` to reload textures.

Next steps:
- browse [docs/OSC-reference.md](docs/OSC-reference.md),
- try richer examples in [SuperCollider/intro.scd](SuperCollider/intro.scd) and [SuperCollider/test.scd](SuperCollider/test.scd).

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
