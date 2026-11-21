// Generated from scene.vosc.json by scripts/generate_example_scd.py.
// Blank renderer: vosc --size 1024x768
// Use a fresh renderer with automatic onset gating or start with scene.vosc.json.
(
var sceneRoot = PathName(thisProcess.nowExecutingPath).pathOnly;
var rendererHost = "127.0.0.1";
var rendererPort = 33333; // change only to a renderer endpoint you own
var rendererRoot = sceneRoot; // set to the scene folder on a remote renderer
var visuals = OSCVisuals(rendererHost, rendererPort, reset: true);

visuals.net.sendBundle(nil,
    ['/layers/reset', 1, "stack"],
    ['/shader', 0, rendererRoot +/+ "assets/sieve.frag"],
    ['/shader/texture', 0, "srctex", rendererRoot +/+ "assets/black_hole.jpg"],
    ['/shader/texture', 0, "sieve", rendererRoot +/+ "assets/square_3x3.png"],
    ['/shader/var', 0, "dim", 1024, 768],
    ['/shader/var', 0, "sievedim", 6, 6],
    ['/shader/var', 0, "multiplier", 3.0, 3.0],
    ['/shader/var', 0, "vColor", 1.0, 1.0, 1.0, 1.0],
    ['/shader/var', 0, "hColor", 0.0, 0.0, 0.0, 1.0],
    ['/onset/force']
);
~visuals = visuals;
)
