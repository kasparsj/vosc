// create visuals sender
~visuals = NetAddr("127.0.0.1", 33333);

// initialize 1 layer
~visuals.sendMsg('/layers', 1, "stack");

// load RotStringsH shader into layer 1
~visuals.sendMsg('/tex', 0, "100fragments/RotStringsV.frag");

// map glow shader var (uniform) to sine function
~visuals.sendMsg('/tex/var', 0, "glow", "sin");

// initialize OSC_VJ SoundStream (only amplitude analysis)
~visuals.sendMsg('/sound/stream', 0, "Microphone");

// map glow shader var (uniform) to channel 1 amplitude
~visuals.sendMsg('/tex/var', 0, "glow", "amp:0");
~visuals.sendMsg('/tex/var/scale', 0, "glow", 10);

// load Ether shader (from shadertoy)
~visuals.sendMsg('/tex', 0, "shadertoy/Ether.frag");

// animate shader color to blue in 10 seconds
~visuals.sendMsg('/tex/color', 0, 0, 0, 255, 10);

// map shader color blue channel to amplitude
~visuals.sendMsg('/tex/color', 0, 255, 255, "amp:0");

// map shader color red channel to tidal
~visuals.sendMsg('/tex/color', 0, "tidal:0", 255, 255);

// unload shader
~visuals.sendMsg('/tex', "*", "");

// change layout to 9 layers grid
~visuals.sendMsg('/layers', 9, "grid");

// load random shaders into all 9 layers
~visuals.sendMsg('/tex/choose', "*", "shader");
