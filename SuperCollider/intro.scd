// create visuals sender
~visuals = NetAddr("127.0.0.1", 33333);

// initialize a grid of 4 layers
~visuals.sendMsg('/layers', 4, "grid");

// load a random image in layer 1
~visuals.sendMsg('/tex/choose', 0, "image");
~visuals.sendMsg('/tex/choose', 0, "image");

// load a random video in layer 2
~visuals.sendMsg('/tex/choose', 1, "video");

// load a random video in layer 3
~visuals.sendMsg('/tex/choose', 2, "shader");

// load a random sketch in layer 4
~visuals.sendMsg('/tex/choose', 3, "sketch");

// unload all layers
~visuals.sendMsg('/tex', "*", "");

// initialize OSC_VJ SoundStream (only amplitude analysis)
~visuals.sendMsg('/sound/stream', 0);

// run Spiral sketch with stroke alpha mapped to amplitude
~visuals.sendMsg('/tex', 0, "Spiral");
~visuals.sendMsg('/tex/var', 0, "strokeAlpha", "amp:0", 20);

// enable "easy" camera
~visuals.sendMsg('/cam', "easy");

// animate camera close-up
~visuals.sendMsg('/cam/pos', 0, 0, 0, 10);
// animate camera back away
~visuals.sendMsg('/cam/pos', 0, 0, 870, 10);

// load penguin 3D model
~visuals.sendMsg('/geom', 0, "penguin/penguin.dae"); // set first layer geometry to the default plane

// orbit camera around penguin (around scene center)
~visuals.sendMsg('/cam/orbit', "easy");