/// PERFORMANCE

~visuals.sendMsg('/clear', 0);
~visuals.sendMsg('/clear', 1);

(
~visuals.sendMsg('/loud/max', "*", 28);
~visuals.sendMsg('/amp/max', "*", 0.1);
~visuals.sendMsg('/thresh', 0, 0);
~visuals.sendMsg('/data', 0, "loud:0");
//~visuals.sendMsg('/data', 0, "rand");
~visuals.sendMsg('/tex', 0, "NoisySpirals");
~visuals.sendMsg('/alpha', 0, 1);
~visuals.sendMsg('/bri', 0, 1.0);
~visuals.sendMsg('/tint', 0, "mfcc", false);
)

~visuals.sendMsg('/tint', 0, 0.15, 0.75, 1.0); // blue
~visuals.sendMsg('/tint', 0, 1.0, 0.45, 0); // orange
~visuals.sendMsg('/tint', 0, 0.4615, 0.615, 1.0); // blue2
~visuals.sendMsg('/tint', 0, 1.0, 1.0, 0.25); // yellow
~visuals.sendMsg('/tint', 0, 0, 0, 0); // black
~visuals.sendMsg('/tint', 0, 1.0, 1.0, 1.0); // white
~visuals.sendMsg('/tint', 0, 0, 0, 0); // white
~visuals.sendMsg('/tint', 0, 1.0, 0, 0); // red
~visuals.sendMsg('/tint', 0, 256.rand, 256.rand, 256.rand); // random
~visuals.sendMsg('/tint', 0, "rand"); // random

(
~visuals.sendMsg('/loud/max', "*", 24);
//~visuals.sendMsg('/data', 1, "loud:0");
~visuals.sendMsg('/data', 1, "noise");
~visuals.sendMsg('/thresh', 1, 0);
~visuals.sendMsg('/tex', 1, "WaveClock");
~visuals.sendMsg('/alpha', 1, 1.0);
~visuals.sendMsg('/bri', 1, 1.0);
~visuals.sendMsg('/tint', 4, "mfcc", true);
)

~visuals.sendMsg('/speed', "*", 1);

~visuals.sendMsg('/data', 0, "loud:0");
~visuals.sendMsg('/alpha', 0, 1.0);
~visuals.sendMsg('/bri', 0, 0);
~visuals.sendMsg('/tint', 0, 1.0, 1.0, 1.0);

~visuals.sendMsg('/alpha', 2, 1.0);
~visuals.sendMsg('/bri', 2, 1.0);

(
//var secs = 60;
var secs = 10;
~visuals.sendMsg('/thresh', 2, 0.7);
~visuals.sendMsg('/data', 2, "rand");
~visuals.sendMsg('/tex', 2, "OscCircle.frag");
~visuals.sendMsg('/tint', 2, 1.0, 1.0, 1.0);
~visuals.sendMsg('/alpha', 2, 0);
~visuals.sendMsg('/alpha', 2, 1.0, secs);
~visuals.sendMsg('/bri', 0, 0, secs);
~visuals.sendMsg('/bri', 1, 0, secs);
{
	secs.wait;
	"bri fade done".postln;
	~visuals.sendMsg('/thresh', "*", 0.1);
}.fork;
)

(
var secs = 30;
~visuals.sendMsg('/thresh', "*", 0.8);
~visuals.sendMsg('/alpha', 0, 0, secs);
~visuals.sendMsg('/alpha', 1, 0, secs);
~visuals.sendMsg('/tint', 2, 0.15, 0.75, 1.0, secs); // lerp to blue
{
	secs.wait;
	//~visuals.sendMsg('/tint', 0, 0, 0, 0);
	~visuals.sendMsg('/tex', 0, "");
	~visuals.sendMsg('/tex', 1, "");
	"colorlerp+alpha done".postln;
}.fork;
)

(
var secs = 30;
// ~visuals.sendMsg('/tint', 2, 1.0, 0.45, 0, secs); // orange
// ~visuals.sendMsg('/tint', 2, 0.4615, 0.615, 1.0, secs); // blue2
~visuals.sendMsg('/tint', 2, 1.0, 1.0, 0.25, secs); // yellow
{
	secs.wait;
	"colorlerp done".postln;
}.fork;
)

(
var secs = 10;
//~visuals.sendMsg('/thresh', "*", 0);
~visuals.sendMsg('/tint', 1, 1.0, 1.0, 1.0);
~visuals.sendMsg('/data', 1, "const");
~visuals.sendMsg('/thresh', 1, 0);
~visuals.sendMsg('/tex', 1, "Spiral");
~visuals.sendMsg('/bri', 1, 1.0);
~visuals.sendMsg('/alpha', 1, 1.0, secs);
{
	secs.wait;
	"spiral fade done".postln;
}.fork;
)



(
var secs = 60;
~visuals.sendMsg('/thresh', 2, 0.2);
~visuals.sendMsg('/data', 2, "loud:0");
~visuals.sendMsg('/tex', 2, "");
~visuals.sendMsg('/alpha', 2, 1.0, secs);
~visuals.sendMsg('/alpha', 0, 0, secs);
~visuals.sendMsg('/alpha', 1, 0, secs);
{
	secs.wait;
	~visuals.sendMsg('/tex', 1, "");
	~visuals.sendMsg('/tex', 0, "");
	~visuals.sendMsg('/data', 3, "loud:0");
	~visuals.sendMsg('/tex', 3, "");
	~visuals.sendMsg('/tint', 3, "mfcc", true);
	~visuals.sendMsg('/data', 4, "loud:0");
	~visuals.sendMsg('/tex', 4, "");
	~visuals.sendMsg('/thresh', "*", 0.8);
}.fork;
)

(
~visuals.sendMsg('/tint', 3, "mfcc", false);
~visuals.sendMsg('/tint', 3, "rand", true);
)

~visuals.sendMsg('/data', 2, "loud:1");
~visuals.sendMsg('/data', 3, "loud:0");
~visuals.sendMsg('/tex', 2, "02.frag");
~visuals.sendMsg('/tex', 2, "04.frag");
~visuals.sendMsg('/tex', 2, "RotStringsH.frag");
~visuals.sendMsg('/alpha', 3, 1.0);
~visuals.sendMsg('/tex', 3, "");
~visuals.sendMsg('/tint', 2, "mfcc", false);
~visuals.sendMsg('/alpha', 2, 1.0);
~visuals.sendMsg('/alpha', 4, 1.0);

(
var secs = 10;
~visuals.sendMsg('/alpha', 2, 0);
~visuals.sendMsg('/alpha', 3, 0);
~visuals.sendMsg('/alpha', 4, 0);
{
	secs.wait;
	"fade out done".postln;
}.fork;
)

(
~visuals.sendMsg('/data', 0, "rand");
~visuals.sendMsg('/data', 1, "const");
~visuals.sendMsg('/data', 2, "const");
~visuals.sendMsg('/data', 3, "rand");
~visuals.sendMsg('/data', 4, "rand");
)

(
~visuals.sendMsg('/tex', 0, "Empty.frag");
~visuals.sendMsg('/tex', 1, "TerrainGrid.frag");
~visuals.sendMsg('/tint', 0, "rand");
~visuals.sendMsg('/thresh', 0, 0.9);
~visuals.sendMsg('/thresh/onset', 0, 0.9);
~visuals.sendMsg('/post', "invert");
)
