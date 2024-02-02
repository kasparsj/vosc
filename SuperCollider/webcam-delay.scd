(
// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

// initialize a grid of 9 layers
~visuals.send('/layers', 9, "grid");

// for each layer do
9.do { |i|
	// load first webcam into the layer
	~visuals.send('/tex', i, "webcam:0");

	// set layer texture history to 120 frames
	~visuals.send('/tex/set', i, "numFrames", 120);

	// set layer texture delay incrementally to 13 seconds
	~visuals.send('/layer/delay', i, i*13);
};
)

// a much more effective implementation, since it downloads webcam data once only
(
// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

// initialize a grid of 9 layers
~visuals.send('/layers', 9, "grid");

// load first webcam into a shared/global texture
~visuals.send('/tex', "t_webcam", "webcam:0");

// set shared texture history to 120 frames
~visuals.send('/tex/set', "t_webcam", "numFrames", 120);

// disable shared texture aspectRatio
~visuals.send('/tex/set', "t_webcam", "aspectRatio", false);

// for each layer do
9.do { |i|
	// load the shared texture into the layer
	~visuals.send('/tex', "*", "t_webcam");

	// set layer texture delay incrementally to 13 seconds
	~visuals.send('/layer/delay', i, i*13);
};
)

// tint layers
9.do { |i| ~visuals.send('/layer/tint', i, [255.rand, 255.rand, 255.rand]); }

// using webcam as texture for 3D geometry
(
// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

// initializes 9 layers with grid layout
~visuals.send('/layers', 9, "grid");

// load first webcam into a shared/global texture
~visuals.send('/tex', "t_webcam", "webcam:0");

// set shared texture history to 120 frames
~visuals.send('/tex/set', "t_webcam", "numFrames", 120);

// disable shared texture aspectRatio
~visuals.send('/tex/set', "t_webcam", "aspectRatio", false);

// load a sphere into a shared/global geometry
~visuals.send('/geom', "g_sphere", "sphere");
9.do { |i|
	// load the shared texture into the layer
	~visuals.send('/tex', i, "t_webcam");

	// load the shared geometry into the layer
	~visuals.send('/geom', i, "g_sphere");

	// set layer texture delay incrementally to 13 seconds
	~visuals.send('/layer/delay', i, i*13);
};
)
