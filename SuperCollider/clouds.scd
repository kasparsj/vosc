(
// create visuals sender
~visuals = NetAddr("127.0.0.1", 33333);

// initialize 1 layer
~visuals.sendMsg('/layers', 1, "stack");

// enable "easy camera"
~visuals.sendMsg('/cam', "easy");

// change camera position & look
~visuals.sendMsg('/cam/pos', 0.1, 2, 3.5);
~visuals.sendMsg('/cam/look', 0, 0, 0);

// change camera clipping settings
~visuals.sendMsg('/cam/set', "nearClip", 0.0001);
~visuals.sendMsg('/cam/set', "farClip", 1024);

// load noise image into layer 1
~visuals.sendMsg('/tex', 0, "noise.png");

// set opengl textureWrap property to GL_REPEAT
~visuals.sendMsg('/tex/set', 0, "textureWrap", 0x2901, 0x2901); // GL_REPEAT

// set some shader variables
~visuals.sendMsg('/shader/var', 0, "fovYScale", (60.degrad/2).tan);
~visuals.sendMsg('/shader/var', 0, "frequency", 1.0);
~visuals.sendMsg('/shader/var', 0, "cloudBaseColor", 1, 1, 1, 1);

// load clouds shader into layer 1
~visuals.sendMsg('/shader', 0, "clouds/RayMarchingCloudsVolumeofCamera");
)