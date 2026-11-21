// Set clip to the absolute path of a local .mov or .mp4 file.
(
var clip = "/absolute/path/to/clip.mov";
if(File.exists(clip).not) { Error("Set clip to an existing video file").throw };
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);
~visuals.send('/layers', 1, "stack");
~visuals.send('/tex', 0, clip);
)

// Seek to halfway after the clip starts playing. Clear the seek trigger
// in the next block so playback continues from the new position.
(
~visuals.send('/tex/seek', 0, 0.5);
~visuals.send('/tex/var', 0, "onset", 1);
)
~visuals.send('/tex/var', 0, "onset", 0);
~visuals.send('/tex/speed', 0, 2.0);
