// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333);

// enable "default" 3D camera
~visuals.send('/cam', "default");

// load penguin 3D model
~visuals.send('/geom', "*", "penguin/penguin.dae");
~visuals.send('/light', 2, [500.rand, 0, 500]);
~visuals.send('/light/remove', 2);

// enable "easy" 3D camera (can be moved with mouse)
~visuals.send('/cam', "easy");

// animate camera close-up
~visuals.send('/cam/pos', [0, 0, "line(300, 10)"]);

// animate camera back away
~visuals.send('/cam/pos', [0, 0, "line(870, 10)"]);

// oscilate camera distance using sine function
~visuals.send('/var', "sine_osc", "200+(sin(t)*1000)");

// map camera's z coordinate to sine_osc variable
~visuals.send('/cam/pos', [0, 0, "sine_osc"]);

// orbit camera around penguin (around scene center)
~visuals.send('/cam/orbit', 20);

~visuals.send('/layer/bri', "*", 5000);

(
z.stop;
z = {
	inf.do {
		~visuals.send('/post', 32.rand);
		36.do { |i|
					~visuals.send('/layer/tint', i, [255.rand, 255.rand, 255.rand]);
		};
		5.wait;
	};
}.fork;
)
