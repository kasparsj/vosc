# vosc

**This is work in progress!**

vosc is a visuals/vjing 2D/3D graphics engine that can be controlled via OSC commands.

It is mainly used within live-coding scenarios sending OSC from SuperCollider / TidalCycles.

See [docs/Home.md](docs/Home.md) for documentation.

See [SuperCollider](SuperCollider) folder for examples.

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
