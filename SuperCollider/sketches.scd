(
~visuals = NetAddr("127.0.0.1", 33333);
//~visuals.sendMsg('/layers', 16, "stack");
~visuals.sendMsg('/tex', 0, "Spiral");
~visuals.sendMsg('/tex/var', 0, "strokeAlpha", "sin");
// ~visuals.sendMsg('/tex/var', 0, "strokeAlpha", "amp:0", 20);
~visuals.sendMsg('/tex/color', 0, "mfcc", true);
)

~visuals.sendMsg('/layer/scale', "*", "");

(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/layers', 2, "stack");
~visuals.sendMsg('/tex', 1, "NoisySpirals");
~visuals.sendMsg('/tex/tint', 0, 255, 255, 0);
~visuals.sendMsg('/tex/var', 1, "strokeAlpha", "amp:1", 20);
~visuals.sendMsg('/tex/color', 1, "mfcc", true);
)

(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/layers', 1, "stack");
~visuals.sendMsg('/tex', 0, "Spiral");
~visuals.sendMsg('/tex/var', 0, "strokeAlpha", "sin");
)

(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/layers', 1, "stack");
~visuals.sendMsg('/tex', 0, "WaveClock");
~visuals.sendMsg('/tex/var', 0, "strokeAlpha", "sin");
)

(
9.do { |i|
	~visuals.sendMsg('/thresh', i, 0);
	~visuals.sendMsg('/tex', i, "WaveClock");
	~visuals.sendMsg('/tex/var', i, "strokeAlpha", "noise");
};
~visuals.sendMsg('/layers/layout', "grid");
)

~visuals.sendMsg('/tex', 0, "black_hole.jpg");
~visuals.sendMsg('/layer/scale', 0, 1);
~visuals.sendMsg('/layer/rot/point', 0, 0.5, 0.5);
~visuals.sendMsg('/layer/rot/speed', 0, 10);