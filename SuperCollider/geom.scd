(
// create visuals sender
~visuals = NetAddr("127.0.0.1", 33333);

// initialize a grid of 9 layers
~visuals.sendMsg('/layers', 9, "grid");

// load plane geometry (optional, because plane is default)
~visuals.sendMsg('/geom', "*", "plane");

// load first webcam in all layers textures
~visuals.sendMsg('/tex', "*", "webcam:0");

// add a light
~visuals.sendMsg('/light', "*", 0, 0, 500);

~visuals.sendMsg('/cam', "easy");
~visuals.sendMsg('/cam/set', "distance", 870);
~visuals.sendMsg('/cam/orbit', 20);
)

~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/tex', 0, "https://apod.nasa.gov/apod/astropix.html");
~visuals.sendMsg('/tex', 0, "https://kaspars.cc");

(
// create visuals sender
~visuals = NetAddr("127.0.0.1", 33333);

// initialize a grid of 9 layers
~visuals.sendMsg('/layers', 9, "grid");

9.do { |i|
	// load (and scale) penguin model in all layers
	~visuals.sendMsg('/geom', i, "penguin/penguin.dae");
	~visuals.sendMsg('/layer/scale', i, 0.7);

	// set random rotation
	~visuals.sendMsg('/layer/rot', i, 360.rand, 360.rand, 360.rand);
};

~visuals.sendMsg('/light', 0, [500.rand, 0, 500]);

~visuals.sendMsg('/cam', "easy");
~visuals.sendMsg('/cam/set', "distance", 870);
~visuals.sendMsg('/cam/orbit', 20);
)
