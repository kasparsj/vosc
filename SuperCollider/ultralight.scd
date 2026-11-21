// Requires a build with USE_OFX_ULTRALIGHT and access to the URL.
(
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);
~visuals.send('/layers', 1, "stack");
~visuals.send('/tex', 0, "https://science.nasa.gov/apod/");
)
