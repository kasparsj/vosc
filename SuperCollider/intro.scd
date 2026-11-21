// Quarks.install("https://github.com/kasparsj/VisualsQuark.git");
// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

// Create four layers and try both layouts.
~visuals.send('/layers', 4);
~visuals.send('/layers/layout', "stack");
~visuals.send('/layers/layout', "grid");

~visuals.send('/tex', 0, "shaders/color.frag");
~visuals.send('/tex/color', 0, [1.0.rand, 1.0.rand, 1.0.rand]);

// load a random image in layer 1
~visuals.send('/tex/choose', 1, "image");

// Center the rotation pivot on every layer.
~visuals.send('/layer/pivot', "*", [0.5, 0.5, 0]);
~visuals.send('/layer/rot', "*", [0, 0, 0]);

// initialize a grid of 4 layers
~visuals.send('/layers/layout', "grid");

// Load a random image in layer 3.
~visuals.send('/tex/choose', 3, "image");

// reset layer 3
~visuals.send('/layer/reset', 3);

(
~scaleCycle.stop;
~scaleCycle = {
	inf.do { |i|
		~visuals.send('/layer/scale', "*", 2.0.rand);
		1.wait;
	}
}.fork;
)

~scaleCycle.stop;
~visuals.send('/layer/scale', "*", 1);

~visuals.send('/layer/tint', "*", [0, 255, 0]);

~visuals.send('/layer/tint', "*", 1.0);

// Load a random sketch in layer 3.
~visuals.send('/tex/choose', 3, "sketch");

// hide layer 3
~visuals.send('/layer/visible', 3, false);

// show layer 3
~visuals.send('/layer/visible', 3, true);

(
~visuals.send('/shader', 3, "
#version 150
uniform float time;
uniform vec2 resolution;
out vec4 fragColor;
void main() {
  vec2 uv = gl_FragCoord.xy/resolution.xy;
  vec3 col = 0.5 + 0.5*cos(time+uv.xyx+vec3(0,2,4));
  fragColor = vec4(col,1.0);
}
");
)

// Load a bundled rainbow shader.
~visuals.send('/tex', 2, "shader:100fragments/RotRainbow.frag");

// solo layer 2 (zero-based index)
~visuals.send('/layer/solo', 2);

// show all layers
~visuals.send('/layer/visible', "*", true);

// Unload layer textures and remove the custom shader.
~visuals.send('/tex/unload', "*");
~visuals.send('/shader/reset', 3);

// Create nine fresh layers in the current layout
~visuals.send('/layers/reset');

// Feed a test amplitude to slot 0. To use a microphone, replace setup
// with /input 0 followed by the device name shown by /input/list.
~visuals.send('/onset', 0); // Apply edits immediately while testing input.
~visuals.send('/input', 0);
~visuals.send('/input/data', 0, 0.65);

// Load an image and map layer opacity to input slot 0.
~visuals.send('/tex', 0, "black_hole.jpg");
~visuals.send('/layer/alpha', 0, "in_0");
