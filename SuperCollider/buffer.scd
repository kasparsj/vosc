(
// create visuals sender
~visuals = OSCVisuals("127.0.0.1", 33333);

~visuals.send('/cam', "easy");
~visuals.send('/geom', 0, "box");
~visuals.send('/geom/set', 0, "drawInstanced", 66*66);
~visuals.send('/var/colors/scheme', "colors", "tetrad", Color(1.0, 0, 1.0, 1.0).hexString, 54);
~visuals.send('/var/nodes', "nodes", ["(-66/2+i)*(20+5)", "(-66/2+i)*(20+5)", "0"], ["(1.0-noise(0, i)*2.0)*t", "(1.0-noise(1, i)*2.0)*t", "(1.0-noise(2, i)*2.0)*t"], "1", 66*66);
~visuals.send('/shader', 0, "instanced/lightShader");
~visuals.send('/shader/buffer', 0, "colors", "colors");
~visuals.send('/shader/buffer', 0, "tex", "nodes");
~visuals.send('/light', 0, [-50, -20, 200]);
)
