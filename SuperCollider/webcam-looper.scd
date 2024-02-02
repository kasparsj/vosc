(
// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

// initialize 1 layer
~visuals.send('/layers', 1, "stack");

// load first webcam into first layer
~visuals.send('/tex', "*", "webcam:0");

//~visuals.send('/input', 0, "Microphone");
//~visuals.send('/input/set', 0, "maxAmp", 0.17);
//~visuals.send('/layer/alpha', "*", "in_0");


// disable aspect ratio
~visuals.send('/tex/set', 0, "aspectRatio", false);

// enable texture looper (2 seconds long loops, 30fps, 2x speed)
~visuals.send('/tex/looper', "*", 2, 30, 2.0);
)