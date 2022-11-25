(
// create visuals sender
~visuals = NetAddr("127.0.0.1", 33333);

// initialize 1 layer
~visuals.sendMsg('/layers', 1, "stack");

// load first webcam into first layer
~visuals.sendMsg('/tex', 0, "webcam:0");

// disable aspect ratio
~visuals.sendMsg('/tex/set', 0, "aspectRatio", false);

// enable texture looper (2 seconds long loops, 30fps, 2x speed)
~visuals.sendMsg('/tex/looper', 0, 2, 30, 2.0);
)