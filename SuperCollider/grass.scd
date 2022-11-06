(
~visuals = NetAddr("127.0.0.1", 33333);

//~visuals.sendMsg('/cam', 1);
~visuals.sendMsg('/cam/nearclip', 0.01);
~visuals.sendMsg('/cam/pos', 0, 0.5, 1.1);
~visuals.sendMsg('/cam/movementmaxspeed', 0.1);

~visuals.sendMsg('/tex', 0, "2d:rect", 1, 21, 0);
~visuals.sendMsg('/tex', 1, "#ff0000");
//~visuals.sendMsg('/tex', 0, "2d:gridplane", 1, 21, 0);
//~visuals.sendMsg('/rot', 0, 0, 0, -90);

~visuals.sendMsg('/shader', 1, "shaders/grass/Grass");
~visuals.sendMsg('/shader/geom/intype', 1, 1);
~visuals.sendMsg('/shader/geom/outtype', 1, 5);
~visuals.sendMsg('/shader/geom/outcount', 1, 14);
~visuals.sendMsg('/geom', 1, "grass", 60, 2, 1.0, 0);
)