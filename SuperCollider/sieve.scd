// Quarks.install("https://github.com/kasparsj/VisualsQuark.git");
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

~visuals.send('/cam', "easy");

~visuals.send('/tex', "square_3x3", "sieves/square_3x3.png");

~createLayer = { |i, w, h, d|
	var prefix = "tex" ++ i;
	// generate colormap
	// generate colorscheme: /var/colors/scheme
	// load scheme into a texture
	// generate palettemap
	~visuals.send('/tex/size', "colormap", 0xffffff.sqrt, 0xffffff.sqrt);
	// todo: GL_NEAREST

	// generate seed (16x16 random)
	~visuals.send('/tex/size', "tex0", w, h, d);
	// todo: SAMPLER_2D_ARRAY
	// todo: GL_NEAREST

	~visuals.send('/tex/data', prefix++"_seed", ["rand(1.0)", "rand(1.0)", "rand(1.0)", 1], w * h * d);

	~visuals.send('/tex', prefix++"_mirror", "shaders/kaleid.frag");
	~visuals.send('/tex/size', prefix++"_mirror", w*2, h*2, d);
	~visuals.send('/shader/tex', prefix++"_mirror", "srctex", prefix++"_seed");

	~visuals.send('/tex', prefix++"_sieve", "shaders/sieve.frag");
	~visuals.send('/tex/size', prefix++"_sieve", w*3, h*3, d);
	~visuals.send('/shader/tex', prefix++"_sieve", "srctex", prefix++"_mirror");
	~visuals.send('/shader/tex', prefix++"_sieve", "sievetex", "square_3x3");

	~visuals.send('/tex/transform', "tex0", "shaders/colormap.frag");

	// todo: create layermaps
}

8.do { |i|
	~createLayer.(i, 16, 16, 16);
};
// todo: render layermapped
// todo: colorweave
// todo: blend all layers together
// todo: post-process