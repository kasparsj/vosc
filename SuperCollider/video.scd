(
// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333);

// set video position to 50%
~visuals.send('/tex/seek', 0, 0.5);

// set onset variable
// ~visuals.send('/tex/var', 0, "onset", "tidal0onset");

// set video speed to 2x
~visuals.send('/tex/var', 0, "speed", 0.01);

// load video
~visuals.send('/tex', 0, "bbb_export.hpv");

// set video postion to rand (updated on onset)
~visuals.send('/tex/seek', 0, "rand");
)