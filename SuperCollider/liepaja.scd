(
// create visuals sender
~visuals = NetAddr("127.0.0.1", 33333);

~visuals.sendMsg('/cam', "easy");
~visuals.sendMsg('/geom', 0, "box");
~visuals.sendMsg('/var/colors', "colors", [1.0, 0, 1.0, 1.0], "tetrad", 54);
~visuals.sendMsg('/shader', 0, "instanced/lightShader");
)