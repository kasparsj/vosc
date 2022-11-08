(
~visuals = NetAddr("127.0.0.1", 33333);

~visuals.sendMsg('/geom', 0, "box", 0.0015, 0.0015, 0.01);
~visuals.sendMsg('/geom/set', 0, "meshMode", 0);
~visuals.sendMsg('/geom/set', 0, "drawInstanced", 128 * 128);

~visuals.sendMsg('/tex/size', "particlesData", 128, 128, 2);
~visuals.sendMsg('/tex/fbo', "particlesData", "numColorbuffers", 2);
~visuals.sendMsg('/tex/fbo', "particlesData", "useDepth", 0);
~visuals.sendMsg('/tex/fbo', "particlesData", "internalformat", 0x8814); // GL_RGBA32F
~visuals.sendMsg('/tex/fbo', "particlesData", "textureTarget", 0x0DE1); // GL_TEXTURE_2D
~visuals.sendMsg('/tex/fbo', "particlesData", "wrapModeHorizontal", 0x812F); // GL_CLAMP_TO_EDGE
~visuals.sendMsg('/tex/fbo', "particlesData", "wrapModeVertical", 0x812F);
~visuals.sendMsg('/tex/fbo', "particlesData", "minFilter", 0x2600); // GL_NEAREST
~visuals.sendMsg('/tex/fbo', "particlesData", "maxFilter", 0x2600);
~visuals.sendMsg('/tex/set', "particlesData", "blendMode", 0);
~visuals.sendMsg('/tex', "particlesData", "shader_pp:particles/Update");
~visuals.sendMsg('/tex/uniform', "particlesData", "timeStep", 1.0 / 60.0);
~visuals.sendMsg('/tex/uniform', "particlesData", "particleMaxAge", 10);
~visuals.sendMsg('/tex/uniform', "particlesData", "noisePositionScale", 1.5);
~visuals.sendMsg('/tex/uniform', "particlesData", "noiseTimeScale", 1.0 / 4000.0);
~visuals.sendMsg('/tex/uniform', "particlesData", "noisePersistence", 0.2);
~visuals.sendMsg('/tex/uniform', "particlesData", "noiseMagnitude", 0.075);
~visuals.sendMsg('/tex/uniform', "particlesData", "baseSpeed", 0.5, 0, 0);

~visuals.sendMsg('/shader', 0, "particles/DrawInstancedGeometry");
~visuals.sendMsg('/tex', 0, "particlesData");
//~visuals.sendMsg('', "u_resolution", particleDataFbo.source()->getWidth(), particleDataFbo.source()->getHeight() );
~visuals.sendMsg('/shader/uniform', 0, "particleMaxAge", 10);
//~visuals.sendMsg('', "numLights", 1 );
~visuals.sendMsg('/shader/uniform', 0, "particleStartColor", 255, 255, 255);
~visuals.sendMsg('/shader/uniform', 0, "particleEndColor", 0, 0, 0);

~visuals.sendMsg('/cam', "easy");
)
