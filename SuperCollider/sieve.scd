(
"gl.scd".loadRelative;

// Quarks.install("https://github.com/kasparsj/VisualsQuark.git");
~visuals = OSCVisuals("127.0.0.1", 33333, reset: true);

~visuals.send('/cam', "easy");

~visuals.send('/tex', "square_3x3", "square_3x3.png");
~visuals.send('/tex/set', "square_3x3", "wrapMode", ~gl[\REPEAT]);
~visuals.send('/tex/set', "square_3x3", "filter", ~gl[\NEAREST]);
~visuals.send('/tex/set', "square_3x3", "static", true);

~visuals.send('/var/colors/scheme', "colors", "tetrad", Color(1.0, 0, 1.0, 1.0), 54);
)


(
var w = h = d = 8;
var paletteMapSize = (0xffffff * 0.5).sqrt.asInteger;

~visuals.send('/tex', "colormap", "palettemap.frag");
~visuals.send('/tex/shader/buffer', "colormap", "colors", "colors");
~visuals.send('/tex/size', "colormap", paletteMapSize.asInteger, paletteMapSize.asInteger);
~visuals.send('/tex/var', "colormap", "algorithm", 0);
~visuals.send('/tex/var', "colormap", "error", 0);
~visuals.send('/tex/var', "colormap", "offset", 0);
~visuals.send('/tex/var', "colormap", "luminanceWeight", 0);
~visuals.send('/tex/set', "colormap", "static", true);

~visuals.send('/shader', "arr_mirror", "array/mirror.frag");
~visuals.send('/shader/var', "arr_mirror", "sizeMult", 2);

~visuals.send('/shader', "arr_sieve", "array/sieve.frag");
~visuals.send('/shader/tex', "arr_sieve", "sieve", "square_3x3");
~visuals.send('/shader/var', "arr_sieve", "vColor", Color(1.0, 1.0, 1.0, 1.0));
~visuals.send('/shader/var', "arr_sieve", "hColor", Color(0.0, 0.0, 0.0, 1.0));
~visuals.send('/shader/var', "arr_sieve", "sizeMult", 3);

~visuals.send('/tex/set', "seed", "filter", ~gl[\NEAREST]);
~visuals.send('/tex/size', "seed", w, h, d);
~visuals.send('/tex/data', "seed", ["rand(1.0)", "rand(1.0)", "rand(1.0)", 1], w * h * d);
~visuals.send('/tex/passes', "seed", "arr_mirror", "arr_sieve");
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