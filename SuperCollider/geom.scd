(
~visuals = NetAddr("127.0.0.1", 33333);

~visuals.sendMsg('/layers', 1, "stack");
~visuals.sendMsg('/geom', 0, "plane");
~visuals.sendMsg('/tex', 0, "webcam:0");
~visuals.sendMsg('/light', 0, 0, 0, 500);
~visuals.sendMsg('/cam', "easy");
~visuals.sendMsg('/cam/set', "distance", 870);
)

(
~visuals = NetAddr("127.0.0.1", 33333);

~visuals.sendMsg('/layers', 1, "stack");
~visuals.sendMsg('/geom', 0, "penguin/penguin.ply");
~visuals.sendMsg('/light', 0, 0, 0, 500);
~visuals.sendMsg('/tex', 0, "webcam:0");
~visuals.sendMsg('/cam', "easy");
~visuals.sendMsg('/cam/set', "distance", 870);
)

(
~visuals = NetAddr("127.0.0.1", 33333);

~visuals.sendMsg('/layers', 1, "stack");
~visuals.sendMsg('/geom', 0, "penguin/penguin.dae");
~visuals.sendMsg('/layer/scale', 0, 0.7);
~visuals.sendMsg('/light', 0, 0, 0, 500);
~visuals.sendMsg('/cam', "easy");
~visuals.sendMsg('/cam/set', "distance", 870);
~visuals.sendMsg('/cam/orbit', 20);
)

~visuals.sendMsg('/sound/set', 0, "maxVol", 0.17);
~visuals.sendMsg('/sound/stream', 0, "default in");