// Open this saved file using SuperCollider with VisualsQuark installed.
// Set ~rendererPort or VOSC_OSC_PORT to match the owned renderer.
// For remote OSC, set ~rendererHost and ~rendererRoot before connecting.
// Camera permission and a connected device are required. Set ~cameraDevice to override device 0.

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
~visuals.send('/layers', 9, "grid");
~visuals.send('/tex', "t_webcam", "webcam:" ++ ((~cameraDevice ? 0).asString), 640, 480);
~visuals.send('/tex', 0, "t_webcam");
)

// Blank renderer only. Evaluate once the live preview appears.
(
~visuals.send('/tex/set', "t_webcam", "numFrames", 120);
~visuals.send('/tex/set', "t_webcam", "aspectRatio", false);
~visuals.send('/tex', "*", "t_webcam");
~visuals.send('/layer/delay', 0, 0);
~visuals.send('/layer/delay', 1, 13);
~visuals.send('/layer/delay', 2, 26);
~visuals.send('/layer/delay', 3, 39);
~visuals.send('/layer/delay', 4, 52);
~visuals.send('/layer/delay', 5, 65);
~visuals.send('/layer/delay', 6, 78);
~visuals.send('/layer/delay', 7, 91);
~visuals.send('/layer/delay', 8, 104);
)
// END GENERATED EXAMPLE SETUP
