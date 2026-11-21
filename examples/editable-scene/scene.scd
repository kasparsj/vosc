// Generated from scene.vosc.json by scripts/generate_example_scd.py.
(
var sceneRoot = PathName(thisProcess.nowExecutingPath).pathOnly;
var rendererHost = "127.0.0.1"; // editable
var rendererPort = 43333; // editable; use an owned private port for tests
var rendererRoot = sceneRoot; // editable for remote/shared filesystems
var width = 640;
var height = 480;
var finalGate = -1;
var replyTimeout = 15.0;
var visuals;

if(OSCVisuals.respondsTo(\sceneReplayVersion).not or: { OSCVisuals.sceneReplayVersion != 1 }) {
    Error("VisualsQuark scene replay API 1 is required; update OSCVisuals before evaluating this scene").throw
};
visuals = OSCVisuals(rendererHost, rendererPort);

visuals.replay({ |visuals|
    // Layout, inputs and shared values
    visuals.send('/layers/reset', 1, "stack");
    // Layer resources
    visuals.send('/shader', 0, "assets/solid.frag");
    // Layer controls
    visuals.send('/layer/pos', 0, [0.0, 0.0, 0.0]);
    visuals.send('/layer/size', 0, [640.0, 480.0, 0.0]);
    visuals.send('/layer/tint', 0, [0.35, 0.6, 1.0, 1.0]);
    visuals.send('/layer/alpha', 0, 0.85);
    visuals.send('/layer/visible', 0, 1);
}, sceneRoot: sceneRoot, width: width, height: height, rendererRoot: rendererRoot,
   finalGate: finalGate, replyTimeout: replyTimeout);
~exportedScene = visuals; // use a different environment variable if preferred
)

// Evaluate live adjustments separately, after replay reports applied:
// ~exportedScene.send('/layer/alpha', 0, 0.5);
