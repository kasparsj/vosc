(
~visuals = OSCVisuals("127.0.0.1", 33333);

~visuals.send('/geom', 0, "box", 0.0015, 0.0015, 0.01);
~visuals.send('/geom/set', 0, "meshMode", 0);
~visuals.send('/geom/set', 0, "drawInstanced", 128 * 128);

~visuals.send('/tex', "particlesData", "shader_pp:particles/Update");
~visuals.send('/tex/size', "particlesData", 128, 128);
~visuals.send('/tex/fbo', "particlesData", "numColorbuffers", 2);
~visuals.send('/tex/fbo', "particlesData", "useDepth", 0);
~visuals.send('/tex/fbo', "particlesData", "internalformat", 0x8814); // GL_RGBA32F
~visuals.send('/tex/fbo', "particlesData", "textureTarget", 0x0DE1); // GL_TEXTURE_2D
~visuals.send('/tex/fbo', "particlesData", "wrapModeHorizontal", 0x812F); // GL_CLAMP_TO_EDGE
~visuals.send('/tex/fbo', "particlesData", "wrapModeVertical", 0x812F);
~visuals.send('/tex/fbo', "particlesData", "minFilter", 0x2600); // GL_NEAREST
~visuals.send('/tex/fbo', "particlesData", "maxFilter", 0x2600);
~visuals.send('/tex/set', "particlesData", "blendMode", 0);
~visuals.send('/tex/var', "particlesData", "timeStep", 1.0 / 60.0);
~visuals.send('/tex/var', "particlesData", "particleMaxAge", 10);
~visuals.send('/tex/var', "particlesData", "noisePositionScale", 1.5);
~visuals.send('/tex/var', "particlesData", "noiseTimeScale", 1.0 / 4000.0);
~visuals.send('/tex/var', "particlesData", "noisePersistence", 0.2);
~visuals.send('/tex/var', "particlesData", "noiseMagnitude", 0.075);
~visuals.send('/tex/var', "particlesData", "baseSpeed", 0.5, 0, 0);
~visuals.send('/tex/data', "particlesData", [0, 0, 0, "rand(10)"], 128 * 128);

~visuals.send('/shader', 0, "particles/DrawInstancedGeometry");
~visuals.send('/tex', 0, "particlesData");
~visuals.send('/shader/var', 0, "particleMaxAge", 10);
~visuals.send('/shader/var', 0, "particleStartColor", 1.0, 1.0, 1.0, 1.0);
~visuals.send('/shader/var', 0, "particleEndColor", 0, 0, 0, 0);
~visuals.send('/shader/var', 0, "texSize", 128, 128);

// ~visuals.send('/cam', "fp");
~visuals.send('/cam', "easy");
~visuals.send('/cam/set', "nearClip", 0.01);
// ~visuals.send('/cam/set', "movementMaxSpeed", 0.01);
~visuals.send('/cam/pos', [0, 0.5, 1.1]);
)
