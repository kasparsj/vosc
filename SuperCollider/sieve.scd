// Use the bundled sieve lookup image to remap a second image.
(
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);
~visuals.send('/layers', 1, "stack");
~visuals.send('/shader', 0, "sieve.frag");
~visuals.send('/shader/tex', 0, "srctex", "black_hole.jpg");
~visuals.send('/shader/tex', 0, "sieve", "square_3x3.png");
~visuals.send('/shader/var', 0, "dim", [1024, 768]);
~visuals.send('/shader/var', 0, "sievedim", [6, 6]);
~visuals.send('/shader/var', 0, "multiplier", [3, 3]);
~visuals.send('/shader/var', 0, "vColor", [1.0, 1.0, 1.0, 1.0]);
~visuals.send('/shader/var', 0, "hColor", [0.0, 0.0, 0.0, 1.0]);
)
