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

// hide layer 1
~visuals.sendMsg('/layer/visible', 0, false);

// show layer 1
~visuals.sendMsg('/layer/visible', 0, true);

// solo layer 2
~visuals.sendMsg('/layer/solo', 1);

// show all layers
~visuals.sendMsg('/layer/visible', "*", true);

// unload all layers
~visuals.sendMsg('/tex', "*", "");

// intialize 1 layer
~visuals.sendMsg('/layers', 1);

// initialize Microphone
~visuals.sendMsg('/mic', 0, "Microphone");

// run Spiral sketch with stroke alpha mapped to mic's amplitude
~visuals.sendMsg('/tex', 0, "Spiral");
~visuals.sendMsg('/tex/var', 0, "strokeAlpha", "mic");
~visuals.sendMsg('/tex/var/range', 0, "strokeAlpha", 50);
