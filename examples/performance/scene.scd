// Open this saved file using SuperCollider with VisualsQuark installed.
// Set ~rendererPort or VOSC_OSC_PORT to match the owned renderer.
// For remote OSC, set ~rendererHost and ~rendererRoot before connecting.
// Evaluate each crossfade separately after the previous cue settles.

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
~visuals.send('/layers', 3, "stack");
~visuals.send('/tex', 0, "NoisySpirals");
~visuals.send('/tex', 1, "WaveClock");
~visuals.send('/tex', 2, ~exampleAssetRoot +/+ "assets/100fragments/OscCircle.frag");
~visuals.send('/layer/tint', 0, [0.15, 0.75, 1.0]);
~visuals.send('/layer/tint', 1, [1.0, 0.45, 0.0]);
~visuals.send('/layer/alpha', "*", 0);
~visuals.send('/layer/alpha', 0, 1);
)
// END GENERATED EXAMPLE SETUP

// Crossfade to the second layer in ten seconds.
(
~visuals.send('/layer/alpha', 0, "line(0,10)");
~visuals.send('/layer/alpha', 1, "line(1,10)");
)

// Crossfade to the third layer once the second cue has settled.
(
~visuals.send('/layer/alpha', 1, "line(0,10)");
~visuals.send('/layer/alpha', 2, "line(1,10)");
)
