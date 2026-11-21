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
~visuals.send('/layers', 1, "stack");
~visuals.send('/geom', 0, "box", 0.0015, 0.0015, 0.01);
~visuals.send('/geom/set', 0, "meshMode", 0);
~visuals.send('/geom/set', 0, "drawInstanced", 16384);
~visuals.send('/tex', "particlesData", ("shader_pp:" ++ (~exampleAssetRoot +/+ "assets/particles/Update.frag")));
~visuals.send('/tex/size', "particlesData", 128, 128);
~visuals.send('/tex/set', "particlesData", "internalformat", 34836);
~visuals.send('/tex/set', "particlesData", "textureTarget", 3553);
~visuals.send('/tex/set', "particlesData", "wrapMode", 33071);
~visuals.send('/tex/set', "particlesData", "filter", 9728);
~visuals.send('/tex/set', "particlesData", "blendMode", 0);
~visuals.send('/tex/var', "particlesData", "timeStep", 0.016666667);
~visuals.send('/tex/var', "particlesData", "particleMaxAge", 2.25);
~visuals.send('/tex/var', "particlesData", "noisePositionScale", 2.05);
~visuals.send('/tex/var', "particlesData", "noiseTimeScale", 0.727);
~visuals.send('/tex/var', "particlesData", "noisePersistence", 0.55);
~visuals.send('/tex/var', "particlesData", "noiseMagnitude", 0.05);
~visuals.send('/tex/var', "particlesData", "baseSpeed", [0.017, 0.19, 0.017]);
~visuals.send('/tex/data', "particlesData", [0, 0, 0, "rand(10.0)"], 16384);
~visuals.send('/shader', 0, ~exampleAssetRoot +/+ "assets/particles/DrawInstancedGeometry.frag");
~visuals.send('/tex', 0, "particlesData");
~visuals.send('/shader/var', 0, "particleMaxAge", 2.25);
~visuals.send('/shader/var', 0, "particleStartColor", [0.050980392, 1.0, 0.8627451, 1.0]);
~visuals.send('/shader/var', 0, "particleEndColor", [0.9411765, 0.2, 0.6039216, 1.0]);
~visuals.send('/shader/var', 0, "texSize", [128.0, 128.0]);
~visuals.send('/shader/var', 0, "baseSpeed", [0.017, 0.19, 0.017]);
~visuals.send('/cam', "default");
~visuals.send('/cam/set', "nearClip", 0.01);
~visuals.send('/cam/pos', 0, 0.4, 1.1);
~visuals.send('/cam/look', 0, 0.2, 0);
)
// END GENERATED EXAMPLE SETUP
