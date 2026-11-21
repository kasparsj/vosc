// Try each built-in sketch separately.
(
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);
~visuals.send('/layers', 1, "stack");
~visuals.send('/tex', 0, "Spiral");
~visuals.send('/tex/var', 0, "strokeAlpha", "0.5+0.5*sin(t)");
)

(
~visuals.send('/tex', 0, "NoisySpirals");
~visuals.send('/layer/tint', 0, [1.0, 0.8, 0.2]);
)

(
~visuals.send('/tex', 0, "WaveClock");
~visuals.send('/layer/tint', 0, [0.4, 0.75, 1.0]);
)
