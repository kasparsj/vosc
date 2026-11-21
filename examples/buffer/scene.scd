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
~visuals.send('/cam', "easy");
~visuals.send('/cam/set', "nearClip", 0.1);
~visuals.send('/cam/set', "farClip", 5000);
~visuals.send('/geom', 0, "box");
~visuals.send('/geom/set', 0, "drawInstanced", 4356);
~visuals.send('/var/colors/scheme', "colors", "tetrad", "#FF00FF", 54);
~visuals.send('/var', "nodes", ["(-66/2+(i%66))*(10+5)", "(66/2-(i/66))*(10+5)", "0"], ["(1.0-noise(0, i)*2.0)*t*60", "(1.0-noise(1, i)*2.0)*t*60", "(1.0-noise(2, i)*2.0)*t*60"], "0.1", 4356);
~visuals.send('/shader', 0, ~exampleAssetRoot +/+ "assets/instanced/lightShader.frag");
~visuals.send('/shader/buffer', 0, "colors", "colors");
~visuals.send('/shader/buffer', 0, "tex", "nodes");
~visuals.send('/shader/var', 0, "lightLocation", [-5e1, -2e1, 2e2]);
~visuals.send('/light', 0, [-50, -20, 200]);
)
// END GENERATED EXAMPLE SETUP

~visuals.send('/cam/set', "farClip", 15000);
~visuals.send('/shading/passes', 0);
