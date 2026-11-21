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
~visuals.send('/layers', 1, "stack");
~visuals.send('/tex', 0, ~exampleAssetRoot +/+ "assets/100fragments/RotStringsV.frag");
~visuals.send('/tex/var', 0, "glow", 1);
)
// END GENERATED EXAMPLE SETUP

// map glow shader var (uniform) to sine function
~visuals.send('/tex/var', 0, "glow", "sin(t*10)");

// Feed a test amplitude to slot 0. To use a microphone, replace setup
// with /input 0 followed by the device name shown by /input/list.
~visuals.send('/onset', 0); // Apply edits immediately while testing input.
~visuals.send('/input', 0);
~visuals.send('/input/data', 0, 0.65);

// map glow shader var (uniform) to input slot 0 amplitude
~visuals.send('/tex/var', 0, "glow", "in_0");

// set shader color to blue
~visuals.send('/tex/color', 0, [0, 0, 1.0]);

~visuals.send('/tex/color', 0, [1.0, 1.0, 0.0]);

// map shader color blue channel to input slot 0 amplitude
~visuals.send('/tex/color', 0, [0.0, 1.0, "in_0"]);

// unload shader
~visuals.send('/tex', "*", "");

// change layout to 9 layers grid
~visuals.send('/layers', 9, "grid");

// Load the same bundled example into all nine layers.
~visuals.send('/tex', "*", "shader:" ++ (~exampleAssetRoot +/+ "assets/100fragments/RotStringsV.frag"));
~visuals.send('/tex/var', "*", "glow", 1.0);

~visuals.send('/input/list');
