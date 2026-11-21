// Start one shared capture. Wait until its live preview is visible.
(
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);
~visuals.send('/layers', 9, "grid");
~visuals.send('/tex', "t_webcam", "webcam:0", 640, 480);
~visuals.send('/tex', 0, "t_webcam");
)

// Enable frame history once the camera has supplied its dimensions.
(
~visuals.send('/tex/set', "t_webcam", "numFrames", 120);
~visuals.send('/tex/set', "t_webcam", "aspectRatio", false);
~visuals.send('/tex', "*", "t_webcam");
9.do { |i|
    ~visuals.send('/layer/delay', i, i * 13);
};
)
