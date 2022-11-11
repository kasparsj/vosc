(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/layers', 1, "stack");
~visuals.sendMsg('/tex', 0, "100fragments/RotStringsH.frag");
~visuals.sendMsg('/tex/var', 0, "glow", "sin");
)