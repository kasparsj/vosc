(
// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333);

// initialize a grid of 9 layers
~visuals.send('/layers', 9, "grid");

// load plane geometry (optional, because plane is default)
~visuals.send('/geom', "*", "plane");

// load first webcam in all layers textures
~visuals.send('/tex', "*", "webcam:0");

// add a light
~visuals.send('/light', "*", [0, 0, 500]);

~visuals.send('/cam', "easy");
~visuals.send('/cam/set', "distance", 870);
~visuals.send('/cam/orbit', 20);
)

~visuals = OSCVisuals("127.0.0.1", 33333);
~visuals.send('/tex', 0, "https://apod.nasa.gov/apod/astropix.html");
~visuals.send('/tex', 0, "https://kaspars.cc");

(
// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333);

// initialize a grid of 9 layers
~visuals.send('/layers', 9, "grid");

9.do { |i|
	// load (and scale) penguin model in all layers
	~visuals.send('/geom', i, "penguin/penguin.dae");
	~visuals.send('/layer/scale', i, 0.7);

	// set random rotation
	~visuals.send('/layer/rot', i, [360.rand, 360.rand, 360.rand]);
};

~visuals.send('/light', 0, [500.rand, 0, 500]);

~visuals.send('/cam', "easy");
~visuals.send('/cam/set', "distance", 870);
~visuals.send('/cam/orbit', 20);
)
