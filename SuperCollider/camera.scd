// create visuals sender
~visuals = NetAddr("127.0.0.1", 33333);

// enable "default" 3D camera
~visuals.sendMsg('/cam', "default");

// load penguin 3D model
~visuals.sendMsg('/geom', 0, "penguin/penguin.dae");
~visuals.sendMsg('/light', 0, 0, 0, 500);

// enable "easy" 3D camera (can be moved with mouse)
~visuals.sendMsg('/cam', "easy");

// animate camera close-up
~visuals.sendMsg('/cam/pos', 0, 0, 300, 10);

// animate camera back away
~visuals.sendMsg('/cam/pos', 0, 0, 870, 10);

// oscilate camera distance using sine function
~visuals.sendMsg('/var', "sine_osc", "sin");
~visuals.sendMsg('/var/range', "sine_osc", 400, 1500);

// map camera's z coordinate to since_osc variable
~visuals.sendMsg('/cam/pos', 0, 0, "sine_osc");

// orbit camera around penguin (around scene center)
~visuals.sendMsg('/cam/orbit', 20);