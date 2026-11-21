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
~visuals.send('/layers', 2, "stack");
~visuals.send('/cam', "default");
~visuals.send('/cam/pos', 0, 400, 900);
~visuals.send('/cam/look', 0, 50, 0);
~visuals.send('/cam/set', "nearClip", 0.01);
~visuals.send('/tex', 0, "2d:gridplane", 1, 21, 0);
~visuals.send('/layer/rot', 0, [0, 0, -90]);
~visuals.send('/geom', 1, "grass", 60, 2, 1.0, 0);
~visuals.send('/shader/set', 1, "geomInputType", 1);
~visuals.send('/shader/set', 1, "geomOutputType", 5);
~visuals.send('/shader/set', 1, "geomOutputCount", 14);
~visuals.send('/shader/var', 1, "stalkHalfWidth", 0.05);
~visuals.send('/shader/var', 1, "stalkHeight", 1.0);
~visuals.send('/shader/var', 1, "animationTimeMaxDifference", 1.0);
~visuals.send('/shader/var', 1, "stalkSwayingMaxAngle", 0.174532925);
~visuals.send('/shader/var', 1, "grassSwayingTimeScale", 1.0);
~visuals.send('/shader/var', 1, "grassSwayingNoiseFrequency", 0.1);
~visuals.send('/shader', 1, ~exampleAssetRoot +/+ "assets/grass/Grass.frag");
)
// END GENERATED EXAMPLE SETUP
