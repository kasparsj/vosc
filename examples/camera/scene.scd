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
~visuals.send('/cam', "default");
~visuals.send('/cam/pos', 0, 0, 1200);
~visuals.send('/cam/look', 0, 0, 0);
~visuals.send('/geom', "penguin", ~exampleAssetRoot +/+ "assets/penguin/penguin.dae");
~visuals.send('/geom', "*", "penguin");
~visuals.send('/light', 0, 0, 0, 1200);
)
// END GENERATED EXAMPLE SETUP

// enable "easy" 3D camera (can be moved with mouse)
~visuals.send('/cam', "easy");
~visuals.send('/cam/set', "distance", 1200);

// animate camera close-up
~visuals.send('/cam/pos', [0, 0, "line(300, 10)"]);

// animate camera back away
~visuals.send('/cam/pos', [0, 0, "line(870, 10)"]);

// oscillate camera distance using sine function
~visuals.send('/var', "sine_osc", "1200+(sin(t)*200)");

// map camera's z coordinate to sine_osc variable
~visuals.send('/cam/pos', [0, 0, "sine_osc"]);

// orbit camera around penguin (around scene center)
~visuals.send('/cam/orbit', 20);

// Enable FXAA, then clear the pass list.
~visuals.send('/shading/passes', "fxaa");
~visuals.send('/shading/passes');
