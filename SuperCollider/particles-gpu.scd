(
~visuals = OSCVisuals("127.0.0.1", 33333);

~visuals.send('/geom', 0, "box", 0.0015, 0.0015, 0.01);
~visuals.send('/geom/set', 0, "meshMode", 0);
~visuals.send('/geom/set', 0, "drawInstanced", 128 * 128);

~visuals.send('/tex/size', "particlesData", 128, 128, 2);
~visuals.send('/tex/fbo', "particlesData", "numColorbuffers", 2);
~visuals.send('/tex/fbo', "particlesData", "useDepth", 0);
~visuals.send('/tex/fbo', "particlesData", "internalformat", 0x8814); // GL_RGBA32F
~visuals.send('/tex/fbo', "particlesData", "textureTarget", 0x0DE1); // GL_TEXTURE_2D
~visuals.send('/tex/fbo', "particlesData", "wrapModeHorizontal", 0x812F); // GL_CLAMP_TO_EDGE
~visuals.send('/tex/fbo', "particlesData", "wrapModeVertical", 0x812F);
~visuals.send('/tex/fbo', "particlesData", "minFilter", 0x2600); // GL_NEAREST
~visuals.send('/tex/fbo', "particlesData", "maxFilter", 0x2600);
~visuals.send('/tex/set', "particlesData", "blendMode", 0);
~visuals.send('/tex', "particlesData", "shader_pp:particles/Update");
~visuals.send('/tex/uniform', "particlesData", "timeStep", 1.0 / 60.0);
~visuals.send('/tex/uniform', "particlesData", "particleMaxAge", 10);
~visuals.send('/tex/uniform', "particlesData", "noisePositionScale", 1.5);
~visuals.send('/tex/uniform', "particlesData", "noiseTimeScale", 1.0 / 4000.0);
~visuals.send('/tex/uniform', "particlesData", "noisePersistence", 0.2);
~visuals.send('/tex/uniform', "particlesData", "noiseMagnitude", 0.075);
~visuals.send('/tex/uniform', "particlesData", "baseSpeed", 0.5, 0, 0);

~visuals.send('/shader', 0, "particles/DrawInstancedGeometry");
~visuals.send('/tex', 0, "particlesData");
//~visuals.send('', "u_resolution", particleDataFbo.source()->getWidth(), particleDataFbo.source()->getHeight() );
~visuals.send('/shader/uniform', 0, "particleMaxAge", 10);
//~visuals.send('', "numLights", 1 );
~visuals.send('/shader/uniform', 0, "particleStartColor", 255, 255, 255);
~visuals.send('/shader/uniform', 0, "particleEndColor", 0, 0, 0);

~visuals.send('/cam', "easy");
)
