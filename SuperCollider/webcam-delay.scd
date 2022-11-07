(
~visuals = NetAddr("127.0.0.1", 33333);
~visuals.sendMsg('/layers', 9, "grid"); // initializes 9 layers with grid layout
9.do { |i|
	~visuals.sendMsg('/tex', i, "webcam:0");
	~visuals.sendMsg('/tex/frames', i, 120);
	~visuals.sendMsg('/rot', i, 0, 0, 0);
	~visuals.sendMsg('/rot/speed', i, 0, 0, 0);
	~visuals.sendMsg('/rot/point', i, "center");
	~visuals.sendMsg('/delay', i, i*13);
};
)