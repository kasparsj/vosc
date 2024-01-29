(
// Quarks.install("https://github.com/kasparsj/VisualsQuark.git");
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);
~visuals.send('/geom', 0, "box", ["rand(-50, 50)", "rand(-50, 50)", "rand(-50, 50)"], [0, 0, 0], [1.0.rand, 0.1, 0.1], 20);
~visuals.send('/cam', "easy");

// todo: when deferred gets turned on, the background stays
~visuals.send('/shading', "deferred");
//~visuals.send('/shading', "forward");
~visuals.send('/shading/passes', "bg", "edge", "ssao", "shadowlight", "pointlight", "bloom");

~visuals.send('/light', 0, [-50, -20, 200]);
)
