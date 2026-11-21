// Minimal editable VOSC scene. Start a renderer on an owned private port first.
// This example intentionally uses 43333, not the default live renderer port 33333.
(
var sceneRoot = PathName(thisProcess.nowExecutingPath).pathOnly;
var rendererHost = "127.0.0.1";
var rendererPort = 43333; // change only to a renderer endpoint you own
var visuals;

// Requires VisualsQuark with scene replay API 1.
if(OSCVisuals.respondsTo(\sceneReplayVersion).not or: { OSCVisuals.sceneReplayVersion != 1 }) {
    Error("Update VisualsQuark: scene replay API 1 is required").throw
};
visuals = OSCVisuals(rendererHost, rendererPort);

visuals.replay({ |visuals|
    // Layout
    visuals.send('/layers/reset', 1, "stack");

    // A solid layer whose fragment shader uses the layer tint and alpha.
    visuals.send('/shader', 0, "assets/solid.frag");
    visuals.send('/layer/pos', 0, [0.0, 0.0, 0.0]);
    visuals.send('/layer/size', 0, [640.0, 480.0, 0.0]);
    visuals.send('/layer/tint', 0, [0.35, 0.6, 1.0, 1.0]);
    visuals.send('/layer/alpha', 0, 0.85);
    visuals.send('/layer/visible', 0, 1);
}, sceneRoot: sceneRoot, width: 640, height: 480);

~exportedScene = visuals;
)

// Evaluate a live adjustment separately, after replay reports applied:
// ~exportedScene.send('/layer/alpha', 0, 0.5);
