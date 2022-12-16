(
~visuals = OSCVisuals("127.0.0.1", 33333);

~visuals.send('/layers', 2, "stack");
~visuals.send('/cam', "easy");
~visuals.send('/cam/pos', [0, 0.5, -1.1]);
~visuals.send('/cam/look', [0, 0, 0]);
~visuals.send('/cam/set', "nearClip", 0.01);
~visuals.send('/cam/set', "movementMaxSpeed", 0.1);

~visuals.send('/tex', 0, "2d:gridplane", 1, 21, 0);
~visuals.send('/layer/rot', 0, [0, 0, -90]);

~visuals.send('/geom', 1, "grass", 60, 2, 1.0, 0);
~visuals.send('/shader/set', 1, "geomInputType", 0x0001); // GL_LINES
~visuals.send('/shader/set', 1, "geomOutputType", 0x0005); // GL_TRIANGLE_STRIP
~visuals.send('/shader/set', 1, "geomOutputCount", 14);
~visuals.send('/shader/var', 1, "stalkHalfWidth", 0.1 / 2);
~visuals.send('/shader/var', 1, "stalkHeight", 1.0);
~visuals.send('/shader/var', 1, "animationTimeMaxDifference", 1.0);
~visuals.send('/shader/var', 1, "stalkSwayingMaxAngle", 20.degrad * 0.5);
~visuals.send('/shader/var', 1, "grassSwayingTimeScale", 1.0);
~visuals.send('/shader/var', 1, "grassSwayingNoiseFrequency", 0.1);
~visuals.send('/shader', 1, "grass/Grass");
)