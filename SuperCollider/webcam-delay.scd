(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/layers', 9, "grid"); // initializes 9 layers with grid layout
9.do { |i|
	~visuals.sendMsg('/tex', i, "webcam:0");
	~visuals.sendMsg('/align', i, "center");
	~visuals.sendMsg('/tex/set', i, "numFrames", 120);
	~visuals.sendMsg('/rot', i, 0, 0, 0);
	~visuals.sendMsg('/rot/speed', i, 0, 0, 0);
	~visuals.sendMsg('/rot/point', i, "center");
	~visuals.sendMsg('/delay', i, i*13);
};
)

(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/layers', 9, "grid"); // initializes 9 layers with grid layout
~visuals.sendMsg('/tex', "t_webcam", "webcam:0");
~visuals.sendMsg('/tex/set', "t_webcam", "numFrames", 120);
~visuals.sendMsg('/tex/set', "t_webcam", "aspectRatio", false);
9.do { |i|
	~visuals.sendMsg('/tex', i, "t_webcam");
	~visuals.sendMsg('/rot', i, 0, 0, 0);
	~visuals.sendMsg('/rot/speed', i, 0, 0, 0);
	~visuals.sendMsg('/rot/point', i, "center");
	~visuals.sendMsg('/delay', i, i*13);
};
)

(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/layers', 9, "grid"); // initializes 9 layers with grid layout
~visuals.sendMsg('/tex', "t_webcam", "webcam:0");
~visuals.sendMsg('/tex/set', "t_webcam", "numFrames", 120);
~visuals.sendMsg('/tex/set', "t_webcam", "aspectRatio", false);
~visuals.sendMsg('/geom', "g_sphere", "sphere");
9.do { |i|
	~visuals.sendMsg('/tex', i, "t_webcam");
	~visuals.sendMsg('/geom', i, "g_sphere");
	~visuals.sendMsg('/rot', i, 0, 0, 0);
	~visuals.sendMsg('/rot/speed', i, 0, 0, 0);
	~visuals.sendMsg('/rot/point', i, "center");
	~visuals.sendMsg('/delay', i, i*13);
};
)

(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/layers', 1, "stack");
~visuals.sendMsg('/tex', 0, "webcam:0");
~visuals.sendMsg('/tex/set', 0, "aspectRatio", false);
~visuals.sendMsg('/tex/loop', 0, 2, 60, 2.0);
)