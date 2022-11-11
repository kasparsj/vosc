(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/layers', 1, "stack");
~visuals.sendMsg('/tex', 0, "NoisySpirals");
//~visuals.sendMsg('/tex/var', 0, "strokeAlpha", "sin");
~visuals.sendMsg('/tex/var', 0, "strokeAlpha", "amp:0", 10);
~visuals.sendMsg('/tex/color', 0, "mfcc", true);
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
