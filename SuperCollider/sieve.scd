"gl.scd".loadRelative;

(
var w = h = d = 16;
var paletteMapSize = (0xffffff * 0.5).sqrt.asInteger;

// Quarks.install("https://github.com/kasparsj/VisualsQuark.git");
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

~visuals.send('/cam', "easy");

~visuals.send('/tex', "square_3x3", "square_3x3.png");

~visuals.send('/var/colors/scheme', "colors", "tetrad", Color(1.0, 0, 1.0, 1.0).hexString, 54);
~visuals.send('/tex', "colormap", "layers/palettemap.frag");
~visuals.send('/tex/shader/buffer', "colormap", "colors", "colors");
~visuals.send('/tex/size', "colormap", paletteMapSize.asInteger, paletteMapSize.asInteger);
~visuals.send('/tex/var', "colormap", "algorithm", 0);
~visuals.send('/tex/var', "colormap", "error", 0);
~visuals.send('/tex/var', "colormap", "offset", 0);
~visuals.send('/tex/var', "colormap", "luminanceWeight", 0);
~visuals.send('/tex/set', "colormap", "static", true);

~visuals.send('/tex/set', "seed", "filter", ~gl[\NEAREST]);
~visuals.send('/tex/size', "seed", w, h, d);
~visuals.send('/tex/data', "seed", ["rand(1.0)", "rand(1.0)", "rand(1.0)", 1], w * h * d);
~visuals.send('/tex/passes', "seed", "layers/mirror.frag");
~visuals.send('/tex/set', "seed", "static", true);

/*~createLayer = { |i, w, h, d|
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
};*/
// todo: render layermapped
// todo: colorweave
// todo: blend all layers together
// todo: post-process
)