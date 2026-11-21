// Three cues for a stacked scene. Evaluate each block separately.
(
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);
~visuals.send('/layers', 3, "stack");
~visuals.send('/tex', 0, "NoisySpirals");
~visuals.send('/tex', 1, "WaveClock");
~visuals.send('/tex', 2, "shaders/100fragments/OscCircle.frag");
~visuals.send('/layer/tint', 0, [0.15, 0.75, 1.0]);
~visuals.send('/layer/tint', 1, [1.0, 0.45, 0.0]);
~visuals.send('/layer/alpha', "*", 0);
~visuals.send('/layer/alpha', 0, 1);
)

// Crossfade to the second layer in ten seconds.
(
~visuals.send('/layer/alpha', 0, "line(0,10)");
~visuals.send('/layer/alpha', 1, "line(1,10)");
)

// Crossfade to the third layer once the second cue has settled.
(
~visuals.send('/layer/alpha', 1, "line(0,10)");
~visuals.send('/layer/alpha', 2, "line(1,10)");
)
