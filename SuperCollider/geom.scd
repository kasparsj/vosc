(
~visuals = NetAddr("127.0.0.1", 33333);

~visuals.sendMsg('/layers', 1, "stack");
~visuals.sendMsg('/geom', 0, "penguin/penguin.ply");
~visuals.sendMsg('/light', 0, 0, 0, 500);
~visuals.sendMsg('/cam', "easy");
~visuals.sendMsg('/cam/set', "distance", 700);
)