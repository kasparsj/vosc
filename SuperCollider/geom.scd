(
// Quarks.install("https://github.com/kasparsj/VisualsQuark.git");
// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

// initialize a grid of 9 layers
~visuals.send('/layers', 9, "grid");

// Load a box on each layer.
~visuals.send('/geom', "*", "box");

// Share a bundled image across the boxes.
~visuals.send('/tex', "boxImage", "black_hole.jpg");
~visuals.send('/tex', "*", "boxImage");

// add a light
~visuals.send('/light', 0, 0, 0, 500);

~visuals.send('/cam', "easy");
~visuals.send('/cam/set', "distance", 1000);
~visuals.send('/cam/orbit', 20);
)

(
// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

// initialize a grid of 100 layers
~visuals.send('/layers', 100, "grid");

~visuals.send('/geom', "penguin", "penguin/penguin.dae");

100.do { |i|
	// load (and scale) penguin model in all layers
	~visuals.send('/geom', i, "penguin");
	~visuals.send('/layer/scale', i, 2.0.rand);

	// set random rotation
	~visuals.send('/layer/rot', i, ["t", "t*2", "t*" ++ 20.rand]);
};

~visuals.send('/light', 0, [500.rand, 0, 500]);
~visuals.send('/light/remove', 0);

~visuals.send('/cam', "easy");
~visuals.send('/cam/set', "distance", 300);
~visuals.send('/cam/orbit', 20);
)

~visuals.send('/shading/passes', 1);
