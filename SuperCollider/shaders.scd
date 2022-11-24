(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/layers', 1, "stack");
~visuals.sendMsg('/tex', 0, "shadertoy/Ether.frag");
~visuals.sendMsg('/tex/color', 0, 0, 0, 255, 10);
~visuals.sendMsg('/tex/var', 0, "test", "tidal:0");
// ~visuals.sendMsg('/tex/var', 0, "glow", "sin");
//~visuals.sendMsg('/tex/var', 0, "glow", "amp:0");
)

~visuals.sendMsg('/tex', "*", "");
~visuals.sendMsg('/layers', 9, "grid");
~visuals.sendMsg('/tex/choose', "*", "shader");

(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/layers', 10, "grid");
~visuals.sendMsg('/tex', "webc", "webcam:0");
~visuals.sendMsg('/tex/set', "webc", "numFrames", 180);
10.do { |i|
	// ~visuals.sendMsg('/tex/var', i, "glow", "sin");
	~visuals.sendMsg('/tex', i, "webc");
	~visuals.sendMsg('/tex/tint', i, 255.rand, 255.rand, 255.rand);
	~visuals.sendMsg('/layer/delay', i, i*10);
	~visuals.sendMsg('/tex/var', i, "glow", "tidal:" ++ i);
}
)
