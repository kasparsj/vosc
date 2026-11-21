// Quarks.install("https://github.com/kasparsj/VisualsQuark.git");
// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

~visuals.send('/layers', 1, "stack");

// enable "default" 3D camera
~visuals.send('/cam', "default");
~visuals.send('/cam/pos', 0, 0, 1200);
~visuals.send('/cam/look', 0, 0, 0);

~visuals.send('/geom', "penguin", "penguin/penguin.dae");

// load penguin 3D model
// ~visuals.send('/geom', "*", "penguin/penguin.dae");
~visuals.send('/geom', "*", "penguin");
~visuals.send('/light', 0, 0, 0, 1200);

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
