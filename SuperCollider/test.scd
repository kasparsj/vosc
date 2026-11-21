// Evaluate one block at a time on a disposable renderer.
(
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);
~visuals.send('/layers', 4, "grid");
~visuals.send('/tex', 0, "black_hole.jpg");
~visuals.send('/tex', 1, "shader:100fragments/OscCircle");
~visuals.send('/tex', 2, "NoisySpirals");
~visuals.send('/tex', 3, "0xff0000");
)

(
~visuals.send('/layer/pivot', 0, [0.5, 0.5, 0]);
~visuals.send('/layer/rot', 0, [0, 0, "t*30"]);
~visuals.send('/layer/tint', 1, [0.15, 0.75, 1.0]);
~visuals.send('/layer/alpha', 2, "0.6+0.4*sin(t)");
~visuals.send('/layer/align', 3, "right", "bottom");
)

(
~visuals.send('/layer/reset', "*");
~visuals.send('/tex/unload', "*");
)
