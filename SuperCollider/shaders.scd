// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333);

// initialize 1 layer
~visuals.send('/layers', 1, "stack");

// load RotStringsH shader into layer 1
~visuals.send('/tex', 0, "shaders/100fragments/RotStringsV.frag");

// map glow shader var (uniform) to sine function
~visuals.send('/tex/var', 0, "glow", "sin");

// initialize Microphone
~visuals.send('/mic', 0, "Microphone");

// map glow shader var (uniform) to mic 1 amplitude
~visuals.send('/tex/var', 0, "glow", "mic:0");
~visuals.send('/tex/var/range', 0, "glow", 10);

// load Ether shader (from shadertoy)
~visuals.send('/tex', 0, "shadertoy/Ether.frag");

// animate shader color to blue in 10 seconds
~visuals.send('/tex/color', 0, 0, 0, 255, 10);

// map shader color blue channel to mic 1 amplitude
~visuals.send('/tex/color', 0, 255, 255, "mic:0");

// map shader color red channel to tidal
~visuals.send('/tex/color', 0, "tidal:0", 255, 255);

// unload shader
~visuals.send('/tex', "*", "");

// change layout to 9 layers grid
~visuals.send('/layers', 9, "grid");

// load random shaders into all 9 layers
~visuals.send('/tex/choose', "*", "shader");
