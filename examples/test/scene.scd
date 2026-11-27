// Open this saved file using SuperCollider with VisualsQuark installed.
// Set ~rendererPort or VOSC_OSC_PORT to match the owned renderer.
// For remote OSC, set ~rendererHost and ~rendererRoot before connecting.

// BEGIN GENERATED EXAMPLE SETUP
// This section is regenerated from scene.vosc.json.
// After native launch, evaluate only this Attach block before the live cues.
(
~exampleAssetRoot = ~rendererRoot ? PathName(thisProcess.nowExecutingPath).pathOnly;
~visuals = OSCVisuals(~rendererHost ? "127.0.0.1",
    ~rendererPort ? ("VOSC_OSC_PORT".getenv ? "33333").asInteger,
    reset: false);
)

// To start from a blank owned renderer (vosc --size 1024x768), evaluate this Setup block instead.
(
// An existing input might have enabled automatic onset gating.
~visuals = OSCVisuals(~rendererHost ? "127.0.0.1",
    ~rendererPort ? ("VOSC_OSC_PORT".getenv ? "33333").asInteger);
~visuals.send('/onset', 0);
~exampleAssetRoot = ~rendererRoot ? PathName(thisProcess.nowExecutingPath).pathOnly;
~visuals = OSCVisuals(~rendererHost ? "127.0.0.1",
    ~rendererPort ? ("VOSC_OSC_PORT".getenv ? "33333").asInteger,
    reset: true);
~visuals.send('/layers', 4, "grid");
~visuals.send('/tex', 0, ~exampleAssetRoot +/+ "assets/black_hole.jpg");
~visuals.send('/tex', 1, ~exampleAssetRoot +/+ "assets/motion-grid.frag");
~visuals.send('/tex', 2, "NoisySpirals");
~visuals.send('/tex', 3, "0xff0000");
)
// END GENERATED EXAMPLE SETUP

(
~visuals.send('/layer/pivot', 0, [0.5, 0.5, 0]);
~visuals.send('/layer/rot', 0, [0, 0, "t*30"]);
~visuals.send('/layer/tint', 1, [0.15, 0.75, 1.0]);
~visuals.send('/layer/alpha', 2, "0.6+0.4*sin(t)");
~visuals.send('/layer/align', 3, "right", "bottom");
)

(
~visuals.send('/layer/reset', "*");
~visuals.send('/tex/unload', "*");
)
