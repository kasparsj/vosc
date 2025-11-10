(
"gl.scd".loadRelative;

// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

// initialize 1 layer
~visuals.send('/layers', 1, "stack");

// enable "easy camera"
~visuals.send('/cam', "easy");

// change camera position & look
~visuals.send('/cam/pos', 0.1, 2, 3.5);
~visuals.send('/cam/look', 0, 0, 0);

// change camera clipping settings
~visuals.send('/cam/set', "nearClip", 0.0001);
~visuals.send('/cam/set', "farClip", 1024);

// load noise image into layer 1
~visuals.send('/tex', 0, "noise.png");

// set opengl textureWrap property to GL_REPEAT
~visuals.send('/tex/set', 0, "wrapModeHorizontal", ~gl[\REPEAT]);
~visuals.send('/tex/set', 0, "wrapModeVertical", ~gl[\REPEAT]);

// set some shader variables
~visuals.send('/shader/var', 0, "fovYScale", (30.degrad/2).tan);
~visuals.send('/shader/var', 0, "frequency", 10.0);
~visuals.send('/shader/var', 0, "cloudBaseColor", [1.0, 0, 0, 1.0]);

// load clouds shader into layer 1
~visuals.send('/shader', 0, "clouds/RayMarchingCloudsVolumeofCamera");
)