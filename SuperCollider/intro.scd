// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333);

// initialize 1 layer
~visuals.send('/layers', 1);

~visuals.send('/tex', 0, "shaders/color.frag");
~visuals.send('/tex/color', 0, [1.0.rand, 1.0.rand, 1.0.rand]);

// load a random image in layer 1
~visuals.send('/tex/choose', 0, "image");

// rotate layer 1
~visuals.send('/layer/pivot', "*", "center", "center");
~visuals.send('/layer/rot', "*", [0, 0, "time"]);

// initialize a grid of 4 layers
~visuals.send('/layers', 4, "grid");

// load a random image in layer 1
~visuals.send('/tex/choose', 0, "image");

// load a random video in layer 2
~visuals.send('/tex/choose', 1, "video");

// load a random video in layer 3
~visuals.send('/layer/unload', 2);

(
z.stop;
z = {
	inf.do { |i|
		~visuals.send('/layer/scale', "*", 10.rand);
		1.wait;
	}
}.fork;
)

~visuals.send('/layer/tint', 0, [0, 255, 0]);

// load a random sketch in layer 4
~visuals.send('/tex/choose', 3, "sketch");

// hide layer 1
~visuals.send('/layer/visible', 0, false);

// show layer 1
~visuals.send('/layer/visible', 0, true);

(
~visuals.send('/shader', 0, "
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

~visuals.send('/shader', 1, "https://www.shadertoy.com/api/v1/shaders/XsyXzw?key=ft8tMh");

~visuals.send('/shader', 0, "https://www.shadertoy.com/view/MtSBDc");

// solo layer 2
~visuals.send('/layer/solo', 1);

// show all layers
~visuals.send('/layer/visible', "*", true);

// unload all layers
~visuals.send('/tex', "*", "");

// intialize 1 layer
~visuals.send('/layers', 1);

// initialize Microphone
~visuals.send('/mic', 0, "Microphone");

// run Spiral sketch with stroke alpha mapped to mic's amplitude
~visuals.send('/geom', 0, "");
~visuals.send('/tex/choose', 0, "shader");
~visuals.send('/tex/alpha', 0, "mic");
~visuals.send('/tex/var/range', 0, "strokeAlpha", 50);
