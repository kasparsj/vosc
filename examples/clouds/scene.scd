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
~visuals.send('/cam', "easy");
~visuals.send('/cam/pos', 0.1, 2, 3.5);
~visuals.send('/cam/look', 0, 0, 0);
~visuals.send('/cam/set', "nearClip", 1e-4);
~visuals.send('/cam/set', "farClip", 1024);
~visuals.send('/tex', 0, ~exampleAssetRoot +/+ "assets/noise.png");
~visuals.send('/tex/set', 0, "wrapMode", 10497);
~visuals.send('/shader/var', 0, "fovYScale", 0.2679492);
~visuals.send('/shader/var', 0, "frequency", 1e1);
~visuals.send('/shader/var', 0, "cloudBaseColor", [1.0, 0.0, 0.0, 1.0]);
~visuals.send('/shader', 0, ~exampleAssetRoot +/+ "assets/clouds/RayMarchingCloudsVolumeofCamera.frag");
)
// END GENERATED EXAMPLE SETUP
