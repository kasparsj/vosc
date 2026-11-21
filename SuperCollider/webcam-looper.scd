// Start the camera and wait for its live preview before enabling the loop.
(
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);
~visuals.send('/layers', 1, "stack");
~visuals.send('/tex', 0, "webcam:0", 640, 480);
)

// Record two-second loops at 30 fps and play them at 2x speed.
// Allow one full recording before judging the loop output.
(
~visuals.send('/tex/set', 0, "aspectRatio", false);
~visuals.send('/tex/looper', 0, 2, 30, 2.0);
)

// Return to the live feed with: ~visuals.send('/tex/looper', 0, 0);
