// create visuals sender
~visuals = NetAddr("127.0.0.1", 33333);

// initialize 1 layer
~visuals.sendMsg('/layers', 1);

// load a random image in layer 1
~visuals.sendMsg('/tex/choose', 0, "image");

// rotate layer 1
~visuals.sendMsg('/layer/pivot', 0, "center", "center");
~visuals.sendMsg('/layer/rot', 0, 0, 0, "time");
~visuals.sendMsg('/var/speed', 0, "rotation", 0.2);

// initialize a grid of 4 layers
~visuals.sendMsg('/layers', 4, "grid");

// load a random image in layer 1
~visuals.sendMsg('/tex/choose', 0, "image");

// load a random video in layer 2
~visuals.sendMsg('/tex/choose', 1, "video");

// load a random video in layer 3
~visuals.sendMsg('/tex/choose', 2, "shader");

// load a random sketch in layer 4
~visuals.sendMsg('/tex/choose', 3, "sketch");

// unload all layers
~visuals.sendMsg('/tex', "*", "");

// intialize 1 layer
~visuals.sendMsg('/layers', 1);

// initialize OSC_VJ SoundStream (only amplitude analysis)
~visuals.sendMsg('/sound/stream', 0, "Microphone");

// run Spiral sketch with stroke alpha mapped to mic's amplitude
~visuals.sendMsg('/tex', 0, "Spiral");
~visuals.sendMsg('/tex/var', 0, "strokeAlpha", 1);
~visuals.sendMsg('/tex/var/range', 0, "strokeAlpha", 50);

// enable "easy" camera (can not be animated)
~visuals.sendMsg('/cam', "easy");

// enable "default" camera (can be animated)
~visuals.sendMsg('/cam', "default");

// animate camera close-up
~visuals.sendMsg('/cam/pos', 0, 0, 100, 10);
// animate camera back away
~visuals.sendMsg('/cam/pos', 0, 0, 870, 10);

// load penguin 3D model
~visuals.sendMsg('/geom', 0, "penguin/penguin.dae");
~visuals.sendMsg('/light', 0, 0, 0, 500);

// orbit camera around penguin (around scene center)
~visuals.sendMsg('/cam/orbit', 20);