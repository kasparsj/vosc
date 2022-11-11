(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/layers', 1, "stack");
~visuals.sendMsg('/tex', 0, "NoisySpirals");
~visuals.sendMsg('/tex/var', 0, "strokeAlpha", "rand");
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
