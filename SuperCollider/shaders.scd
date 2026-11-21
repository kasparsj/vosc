// Quarks.install("https://github.com/kasparsj/VisualsQuark.git");
// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);
// ~visuals = OSCVisuals("192.168.8.240", 33333, reset: true);

// initialize 1 layer
~visuals.send('/layers', 1, "stack");

// load RotStringsV shader into layer 1
~visuals.send('/tex', 0, "shaders/100fragments/RotStringsV.frag");
~visuals.send('/tex/var', 0, "glow", 1);

// map glow shader var (uniform) to sine function
~visuals.send('/tex/var', 0, "glow", "sin(t*10)");

// Feed a test amplitude to slot 0. To use a microphone, replace setup
// with /input 0 followed by the device name shown by /input/list.
~visuals.send('/onset', 0); // Apply edits immediately while testing input.
~visuals.send('/input', 0);
~visuals.send('/input/data', 0, 0.65);

// map glow shader var (uniform) to input slot 0 amplitude
~visuals.send('/tex/var', 0, "glow", "in_0");

// set shader color to blue
~visuals.send('/tex/color', 0, [0, 0, 1.0]);

~visuals.send('/tex/color', 0, [1.0, 1.0, 0.0]);

// map shader color blue channel to input slot 0 amplitude
~visuals.send('/tex/color', 0, [0.0, 1.0, "in_0"]);

// unload shader
~visuals.send('/tex', "*", "");

// change layout to 9 layers grid
~visuals.send('/layers', 9, "grid");

// Load the same bundled example into all nine layers.
~visuals.send('/tex', "*", "shader:100fragments/RotStringsV.frag");
~visuals.send('/tex/var', "*", "glow", 1.0);

~visuals.send('/input/list');
