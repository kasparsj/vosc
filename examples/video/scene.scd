// Open this saved file using SuperCollider with VisualsQuark installed.
// Set ~rendererPort or VOSC_OSC_PORT to match the owned renderer.
// For remote OSC, set ~rendererHost and ~rendererRoot before connecting.
// Set ~clip or VOSC_VIDEO_CLIP before blank-renderer setup.

// BEGIN GENERATED EXAMPLE SETUP
// This section is regenerated from scene.vosc.json.
// After native launch, evaluate only this Attach block before the live cues.
(
~exampleAssetRoot = ~rendererRoot ? PathName(thisProcess.nowExecutingPath).pathOnly;
~visuals = OSCVisuals(~rendererHost ? "127.0.0.1",
    ~rendererPort ? ("VOSC_OSC_PORT".getenv ? "33333").asInteger,
    reset: false);
)

// To start from a blank owned renderer (vosc --size 1024x768), evaluate this Setup block instead.
(
var clip = ~clip ? "VOSC_VIDEO_CLIP".getenv;
var validClip = clip.isString
    and: {PathName(clip).isAbsolutePath}
    and: {clip.toLower.endsWith(".mov") or: {clip.toLower.endsWith(".mp4")}}
    and: {File.exists(clip)};
if(validClip.not) {
    Error("Set ~clip or VOSC_VIDEO_CLIP to an existing renderer-local MOV or MP4").throw;
};
// An existing input might have enabled automatic onset gating.
~visuals = OSCVisuals(~rendererHost ? "127.0.0.1",
    ~rendererPort ? ("VOSC_OSC_PORT".getenv ? "33333").asInteger);
~visuals.send('/onset', 0);
~exampleAssetRoot = ~rendererRoot ? PathName(thisProcess.nowExecutingPath).pathOnly;
~visuals = OSCVisuals(~rendererHost ? "127.0.0.1",
    ~rendererPort ? ("VOSC_OSC_PORT".getenv ? "33333").asInteger,
    reset: true);
~visuals.send('/layers', 1, "stack");
~visuals.send('/tex', 0, clip);
)
// END GENERATED EXAMPLE SETUP

// Seek to halfway after the clip starts playing. Clear the seek trigger
// in the next block so playback continues from the new position.
(
~visuals.send('/tex/seek', 0, 0.5);
~visuals.send('/tex/var', 0, "onset", 1);
)
~visuals.send('/tex/var', 0, "onset", 0);
~visuals.send('/tex/speed', 0, 2.0);
