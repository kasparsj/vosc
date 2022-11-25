(
// create visuals sender
~visuals = NetAddr("127.0.0.1", 33333);

// initialize a grid of 9 layers
~visuals.sendMsg('/layers', 9, "grid");

// for each layer do
9.do { |i|
	// load first webcam into the layer
	~visuals.sendMsg('/tex', i, "webcam:0");

	// set layer texture history to 120 frames
	~visuals.sendMsg('/tex/set', i, "numFrames", 120);

	// set layer texture delay incrementally to 13 seconds
	~visuals.sendMsg('/layer/delay', i, i*13);
};
)

// a much more effective implementation, since it downloads webcam data once only
(
// create visuals sender
~visuals = NetAddr("127.0.0.1", 33333);

// initialize a grid of 9 layers
~visuals.sendMsg('/layers', 9, "grid");

// load first webcam into a shared/global texture
~visuals.sendMsg('/tex', "t_webcam", "webcam:0");

// set shared texture history to 120 frames
~visuals.sendMsg('/tex/set', "t_webcam", "numFrames", 120);

// disable shared texture aspectRatio
~visuals.sendMsg('/tex/set', "t_webcam", "aspectRatio", false);

// for each layer do
9.do { |i|
	// load the shared texture into the layer
	~visuals.sendMsg('/tex', i, "t_webcam");

	// set layer texture delay incrementally to 13 seconds
	~visuals.sendMsg('/layer/delay', i, i*13);
};
)

// tint layers
9.do { |i| ~visuals.sendMsg('/layer/tint', i, 255.rand, 255.rand, 255.rand); }

// using webcam as texture for 3D geometry
(
// create visuals sender
~visuals = NetAddr("127.0.0.1", 33333);

// initializes 9 layers with grid layout
~visuals.sendMsg('/layers', 9, "grid");

// load first webcam into a shared/global texture
~visuals.sendMsg('/tex', "t_webcam", "webcam:0");

// set shared texture history to 120 frames
~visuals.sendMsg('/tex/set', "t_webcam", "numFrames", 120);

// disable shared texture aspectRatio
~visuals.sendMsg('/tex/set', "t_webcam", "aspectRatio", false);

// load a sphere into a shared/global geometry
~visuals.sendMsg('/geom', "g_sphere", "sphere");
9.do { |i|
	// load the shared texture into the layer
	~visuals.sendMsg('/tex', i, "t_webcam");

	// load the shared geometry into the layer
	~visuals.sendMsg('/geom', i, "g_sphere");

	// set layer texture delay incrementally to 13 seconds
	~visuals.sendMsg('/layer/delay', i, i*13);
};
)
