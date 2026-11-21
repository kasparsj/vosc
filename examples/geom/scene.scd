// Open this saved file using SuperCollider with VisualsQuark installed.
// Set ~rendererPort or VOSC_OSC_PORT to match the owned renderer.
// For remote OSC, set ~rendererHost and ~rendererRoot before connecting.

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
// An existing input might have enabled automatic onset gating.
~visuals = OSCVisuals(~rendererHost ? "127.0.0.1",
    ~rendererPort ? ("VOSC_OSC_PORT".getenv ? "33333").asInteger);
~visuals.send('/onset', 0);
~exampleAssetRoot = ~rendererRoot ? PathName(thisProcess.nowExecutingPath).pathOnly;
~visuals = OSCVisuals(~rendererHost ? "127.0.0.1",
    ~rendererPort ? ("VOSC_OSC_PORT".getenv ? "33333").asInteger,
    reset: true);
~visuals.send('/layers', 9, "grid");
~visuals.send('/geom', "*", "box");
~visuals.send('/tex', "boxImage", ~exampleAssetRoot +/+ "assets/black_hole.jpg");
~visuals.send('/tex', "*", "boxImage");
~visuals.send('/light', 0, 0, 0, 500);
~visuals.send('/cam', "easy");
~visuals.send('/cam/set', "distance", 1000);
~visuals.send('/cam/orbit', 20);
)
// END GENERATED EXAMPLE SETUP

(
// create visuals sender
~visuals = OSCVisuals(~rendererHost ? "127.0.0.1",
    ~rendererPort ? ("VOSC_OSC_PORT".getenv ? "33333").asInteger, reset: true);

// initialize a grid of 100 layers
~visuals.send('/layers', 100, "grid");

~visuals.send('/geom', "penguin", ~exampleAssetRoot +/+ "assets/penguin/penguin.dae");

100.do { |i|
	// load (and scale) penguin model in all layers
	~visuals.send('/geom', i, "penguin");
	~visuals.send('/layer/scale', i, 2.0.rand);

	// set random rotation
	~visuals.send('/layer/rot', i, ["t", "t*2", "t*" ++ 20.rand]);
};

~visuals.send('/light', 0, [500.rand, 0, 500]);
~visuals.send('/light/remove', 0);

~visuals.send('/cam', "easy");
~visuals.send('/cam/set', "distance", 300);
~visuals.send('/cam/orbit', 20);
)

~visuals.send('/shading/passes', 1);
