(
~visuals = NetAddr("127.0.0.1", 33333);

~visuals.sendMsg('/layers', 4, "stack");
~visuals.sendMsg('/cam', "easy");
~visuals.sendMsg('/cam/pos', 0, 0.5, -1.1);
~visuals.sendMsg('/cam/look', 0, 0, 0);
~visuals.sendMsg('/cam/set', "nearClip", 0.01);
~visuals.sendMsg('/cam/set', "movementMaxSpeed", 0.1);

~visuals.sendMsg('/tex', 0, "2d:gridplane", 1, 21, 0);
~visuals.sendMsg('/rot', 0, 0, 0, -90);

~visuals.sendMsg('/geom', 1, "grass", 60, 2, 1.0, 0);
~visuals.sendMsg('/shader/set', 1, "geomInputType", 0x0001); // GL_LINES
~visuals.sendMsg('/shader/set', 1, "geomOutputType", 0x0005); // GL_TRIANGLE_STRIP
~visuals.sendMsg('/shader/set', 1, "geomOutputCount", 14);
~visuals.sendMsg('/shader/uniform', 1, "stalkHalfWidth", 0.1 / 2);
~visuals.sendMsg('/shader/uniform', 1, "stalkHeight", 1.0);
~visuals.sendMsg('/shader/uniform', 1, "animationTimeMaxDifference", 1.0);
~visuals.sendMsg('/shader/uniform', 1, "stalkSwayingMaxAngle", 20.degrad * 0.5);
~visuals.sendMsg('/shader/uniform', 1, "grassSwayingTimeScale", 1.0);
~visuals.sendMsg('/shader/uniform', 1, "grassSwayingNoiseFrequency", 0.1);
~visuals.sendMsg('/shader', 1, "grass/Grass");
)