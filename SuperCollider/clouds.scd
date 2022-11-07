(
~visuals = NetAddr("127.0.0.1", 33333);

~visuals.sendMsg('/layers', 4, "stack");

~visuals.sendMsg('/cam', "easy");
~visuals.sendMsg('/cam/set', "autoDistance", false);
~visuals.sendMsg('/cam/set', "globalPosition", 0.1, 2, -3.5);
~visuals.sendMsg('/cam/set', "nearClip", 0.0001);
~visuals.sendMsg('/cam/set', "farClip", 1024);

~visuals.sendMsg('/tex', 0, "noise.png");

~visuals.sendMsg('/shader', 0, "clouds/RayMarchingCloudsVolumeofCamera");
~visuals.sendMsg('/shader/uniform', 0, "fovYScale", (60.degrad/2).tan);
~visuals.sendMsg('/shader/uniform', 0, "frequency", 1.0);
~visuals.sendMsg('/shader/uniform', 0, "cloudBaseColor", 255, 255, 255);
)