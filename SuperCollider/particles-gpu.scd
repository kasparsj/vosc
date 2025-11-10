(
"gl.scd".loadRelative;

// Quarks.install("https://github.com/kasparsj/VisualsQuark.git");
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

~visuals.send('/geom', 0, "box", 0.0015, 0.0015, 0.01);
~visuals.send('/geom/set', 0, "meshMode", 0);
~visuals.send('/geom/set', 0, "drawInstanced", 128 * 128);

~visuals.send('/tex', "particlesData", "shader_pp:particles/Update");
~visuals.send('/tex/size', "particlesData", 128, 128);
~visuals.send('/tex/fbo', "particlesData", "numColorbuffers", 2);
~visuals.send('/tex/fbo', "particlesData", "useDepth", 0);
~visuals.send('/tex/set', "particlesData", "internalformat", ~gl[\RGBA32F]);
~visuals.send('/tex/set', "particlesData", "textureTarget", ~gl[\TEXTURE_2D]);
~visuals.send('/tex/set', "particlesData", "wrapModeHorizontal", ~gl[\CLAMP_TO_EDGE]);
~visuals.send('/tex/set', "particlesData", "wrapModeVertical", ~gl[\CLAMP_TO_EDGE]);
~visuals.send('/tex/set', "particlesData", "filter", ~gl[\NEAREST]);
~visuals.send('/tex/set', "particlesData", "blendMode", 0);
~visuals.send('/tex/var', "particlesData", "timeStep", 1.0 / 60.0);
~visuals.send('/tex/var', "particlesData", "particleMaxAge", 2.25);
~visuals.send('/tex/var', "particlesData", "noisePositionScale", 2.05);
~visuals.send('/tex/var', "particlesData", "noiseTimeScale", 0.727);
~visuals.send('/tex/var', "particlesData", "noisePersistence", 0.55);
~visuals.send('/tex/var', "particlesData", "noiseMagnitude", 0.05);
~visuals.send('/tex/var', "particlesData", "baseSpeed", 0.017, 0.19, 0.017);
~visuals.send('/tex/data', "particlesData", [0, 0, 0, "rand(10.0)"], 128 * 128);

~visuals.send('/shader', 0, "particles/DrawInstancedGeometry");
~visuals.send('/tex', 0, "particlesData");
~visuals.send('/shader/var', 0, "particleMaxAge", 2.25);
~visuals.send('/shader/var', 0, "particleStartColor", [13, 255, 220, 255] / 255);
~visuals.send('/shader/var', 0, "particleEndColor", [240, 51, 154, 255] / 255);
~visuals.send('/shader/var', 0, "texSize", 128, 128);

 ~visuals.send('/cam', "fp");
~visuals.send('/cam/set', "nearClip", 0.01);
~visuals.send('/cam/set', "movementMaxSpeed", 0.01);
~visuals.send('/cam/pos', [0, 0.5, 1.1]);
)

~visuals.send('/cam/set', "nearClip", 0.01);
 ~visuals.send('/cam', "easy");