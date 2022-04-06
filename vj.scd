m = ServerMeter.new(s, 2, 2);

(
~visuals = NetAddr("127.0.0.1", 33333);
~fakeDirtPlay = false;
~synths = [];
// ~file = "/Users/kasparsj/Music/ivars/tema1.wav";
// ~file = "/Users/kasparsj/Music/ivars/tema1_2.wav";
// ~file = "/Users/kasparsj/Music/ivars/tema1_1.wav";
if (~file.isNil, {
	~numChannels = 2;
}, {
	~buffers = [];
	Buffer.read(s, ~file, action: { |buffer|
		~numChannels = buffer.numChannels;
		~numChannels.do { |i|
			~buffers = ~buffers.add(Buffer.readChannel(s, ~file, channels: [i]));
		};
	});
});
)

(
SynthDef(\soundin, {|out=0, in=0, rate=60|
	var input, amp, chain, onset, loudness, mfcc, trig;
    input = SoundIn.ar(in);
	chain = FFT(LocalBuf(1024), input);
	onset = Onsets.kr(chain);
	amp = Amplitude.kr(input);
    loudness = Loudness.kr(chain);
	mfcc = MFCC.kr(chain, 3);
    trig = Impulse.kr(rate);
    SendReply.kr(trig, '/visuals', [in, onset, amp, loudness] ++ mfcc);
	Out.ar(out, input);
}).add;

SynthDef(\buf, {|out=0, in=0, rate=60, bufnum|
	var input, amp, chain, onset, loudness, mfcc, trig;
	input = PlayBuf.ar(1, bufnum, BufRateScale.kr(bufnum), loop: 1);
	chain = FFT(LocalBuf(1024), input);
	onset = Onsets.kr(chain);
	amp = Amplitude.kr(input);
    loudness = Loudness.kr(chain);
	mfcc = MFCC.kr(chain, 3);
    trig = Impulse.kr(rate);
    SendReply.kr(trig, '/visuals', [in, onset, amp, loudness] ++ mfcc);
	Out.ar(out, Splay.ar(input));
}).add;
)

(
OSCdef(\visuals_sender, {|msg|
    var data = msg[3..];
	var in = data[0];
	var onset = data[1];
	var amp = data[2];
	var loudness = data[3];
	var mfcc = data[4..];
	~visuals.sendMsg('/sound/data', in, amp, loudness, onset.asInteger, *mfcc);
	if (~fakeDirtPlay and: {onset == 1}, {
		~visuals.sendMsg('/dirt/play', \s, "ch"++in, \amp, amp, \loudness, loudness);
	});
}, '/visuals');
)

(
var synthName = if (~file.isNil, { \soundin }, { \buf });
~numChannels.do {|i|
	var bufnum = if (~file.isNil, { nil }, { ~buffers[i].bufnum; });
	~synths = ~synths.add(Synth(synthName, [in: i, rate: 120, bufnum: bufnum]));
};
)

(
~synths.do { |synth|
	synth.free;
}
)

~visuals.sendMsg('/layers', 8);
~visuals.sendMsg('/layout', 3);
~visuals.sendMsg('/loud/thresh', 0);
~visuals.sendMsg('/loud/max', 32);
~visuals.sendMsg('/amp/thresh', 0.35);
~visuals.sendMsg('/amp/max', 0.1);

~visuals.sendMsg('/speed', "*", 0.5);
~visuals.sendMsg('/choose', "*", "shader");
~visuals.sendMsg('/reload', "*");
~visuals.sendMsg('/noclear', "*", true);
~visuals.sendMsg('/load', "*", "shader:OscCircle"); // good
~visuals.sendMsg('/load', "*", "shader:NoisySpirals");
~visuals.sendMsg('/pos', "*", 0, 0);
~visuals.sendMsg('/size', "*", 960, 1080);
~visuals.sendMsg('/data', "*", "amp");
~visuals.sendMsg('/data', "*", "loud");
~visuals.sendMsg('/color', "*", 256.rand, 256.rand, 256.rand);
~visuals.sendMsg('/load', "*", "");
~visuals.sendMsg('/unload', "*");
~visuals.sendMsg('/choose', "*", "video");
~visuals.sendMsg('/seek/random', "*");
~visuals.sendMsg('/reset', "*");

~visuals.sendMsg('/color', 0, 0.15, 0.75, 1.0); // blue
~visuals.sendMsg('/color', 0, 1.0, 0.45, 0); // orange
~visuals.sendMsg('/color', 0, 0.4615, 0.615, 1.0); // blue2
~visuals.sendMsg('/color', 0, 1.0, 1.0, 0.25); // yellow
~visuals.sendMsg('/color', 0, 0, 0, 0); // black
~visuals.sendMsg('/color', 0, 1.0, 1.0, 1.0); // white
~visuals.sendMsg('/color', 1, 0, 0, 0); // white
~visuals.sendMsg('/color', 0, 1.0, 0, 0); // red
~visuals.sendMsg('/color', 0, 256.rand, 256.rand, 256.rand); // random
~visuals.sendMsg('/color/random', 0); // random

//~visuals.sendMsg('/onset/force'); // force onset
//~visuals.sendMsg('/onset', false); // toggle onset
~visuals.sendMsg('/data', 0, "const");
~visuals.sendMsg('/data', 0, "rand");
~visuals.sendMsg('/data', 0, "noise");
~visuals.sendMsg('/data', 1, "noise");
~visuals.sendMsg('/data', "*", "loud:0");
~visuals.sendMsg('/load', 0, "shader:AtmoScattering");
~visuals.sendMsg('/load', 0, "sketch:WaveClock");
~visuals.sendMsg('/load', 1, "sketch:NoisySpirals");
~visuals.sendMsg('/reset', 0);

/// PERFORMANCE

~visuals.sendMsg('/clear', 0);
~visuals.sendMsg('/clear', 1);

(
~visuals.sendMsg('/loud/max', 32);
~visuals.sendMsg('/amp/max', 0.1);
//~visuals.sendMsg('/data', 0, "loud:1");
~visuals.sendMsg('/data', 0, "noise");
~visuals.sendMsg('/load', 0, "sketch:NoisySpirals");
~visuals.sendMsg('/alpha', 0, 1);
~visuals.sendMsg('/bri', 0, 1.0);
~visuals.sendMsg('/color/mfcc', 0, false);
)

~visuals.sendMsg('/color', 2, 0.15, 0.75, 1.0); // blue
~visuals.sendMsg('/color', 2, 1.0, 0.45, 0); // orange
~visuals.sendMsg('/color', 0, 0.4615, 0.615, 1.0); // blue2
~visuals.sendMsg('/color', 2, 1.0, 1.0, 0.25); // yellow
~visuals.sendMsg('/color', 0, 0, 0, 0); // black
~visuals.sendMsg('/color', 2, 1.0, 1.0, 1.0); // white
~visuals.sendMsg('/color', 1, 0, 0, 0); // white
~visuals.sendMsg('/color', 0, 1.0, 0, 0); // red
~visuals.sendMsg('/color', 0, 256.rand, 256.rand, 256.rand); // random
~visuals.sendMsg('/color/random', 0); // random

(
~visuals.sendMsg('/loud/max', 24);
~visuals.sendMsg('/data', 1, "loud:1");
~visuals.sendMsg('/load', 1, "sketch:WaveClock");
~visuals.sendMsg('/alpha', 1, 1.0);
~visuals.sendMsg('/bri', 1, 1.0);
~visuals.sendMsg('/color/mfcc', 1, false);
)

~visuals.sendMsg('/speed', "*", 1);

(
~visuals.sendMsg('/load', 0, "shader:OscCircle");
~visuals.sendMsg('/loud/thresh', 0.7);
~visuals.sendMsg('/color', 0, 1.0, 1.0, 1.0);
{
	100.do { |i|
		var j = (i+1);
		~visuals.sendMsg('/alpha', 0, j/100.0);
		~visuals.sendMsg('/bri', 0, 1-(j/100.0));
		~visuals.sendMsg('/bri', 1, 1-(j/100.0));
		0.6.wait;
	};
	~visuals.sendMsg('/loud/thresh', 0.6);
	"bri fade done".postln;
}.fork;
)

(
~visuals.sendMsg('/loud/thresh', 0.8);
{
	// lerp to blue
	100.do { |i|
		var j = (i+1);
		~visuals.sendMsg('/color/lerp', 0, j/100.0, 1.0, 1.0, 1.0, 0.15, 0.75, 1.0);
		~visuals.sendMsg('/alpha', 0, 1-(j/100.0));
		~visuals.sendMsg('/alpha', 1, 1-(j/100.0));
		0.3.wait;
	};
	//~visuals.sendMsg('/color', 0, 0, 0, 0);
	~visuals.sendMsg('/load', 0, "");
	~visuals.sendMsg('/load', 1, "");
	"colorlerp+alpha done".postln;
}.fork;
)

(
{
	// lerp to orange
	100.do { |i|
		var j = (i+1);
		// ~visuals.sendMsg('/color/lerp', 0, j/100.0, 0.15, 0.75, 1.0, 1.0, 0.45, 0); // orange
		// ~visuals.sendMsg('/color/lerp', 0, j/100.0, 1.0, 1.0, 0.45, 0, 0.4615, 0.615, 1.0); // blue2
		~visuals.sendMsg('/color/lerp', 0, j/100.0, 0.4615, 0.615, 1.0, 1.0, 1.0, 0.25); // yellow
		0.3.wait;
	};
	"colorlerp done".postln;
}.fork;
)

(
{
	//~visuals.sendMsg('/loud/thresh', 0);
	~visuals.sendMsg('/load', 1, "sketch:Spiral");
	~visuals.sendMsg('/bri', 1, 1.0);
	100.do { |i|
		var j = (i+1);
		~visuals.sendMsg('/alpha', 1, (j/100.0));
		0.1.wait;
	};
}.fork;
)



(
~visuals.sendMsg('/data', 2, "const");
~visuals.sendMsg('/load', 2, "StarNest");
{
	100.do { |i|
		var j = (i+1);
		~visuals.sendMsg('/alpha', 2, j/100.0);
		~visuals.sendMsg('/alpha', 0, 1-(j/100.0));
		~visuals.sendMsg('/alpha', 1, 1-(j/100.0));
		0.6.wait;
	};
	~visuals.sendMsg('/load', 1, "");
	~visuals.sendMsg('/load', 0 "");
	~visuals.sendMsg('/data', 3, "loud:1");
	~visuals.sendMsg('/load', 3, "shader:Ether");
	~visuals.sendMsg('/color/mfcc', 3, true);
	~visuals.sendMsg('/data', 4, "loud:0");
	~visuals.sendMsg('/load', 4, "shader:AtmoScattering");
	~visuals.sendMsg('/loud/thresh', 0.8);
}.fork;
)

(
~visuals.sendMsg('/color/mfcc', 3, false);
~visuals.sendMsg('/color/random', 3, true);
)

~visuals.sendMsg('/data', 2, "loud:1");
~visuals.sendMsg('/data', 3, "loud:0");
~visuals.sendMsg('/load', 2, "shader:02");
~visuals.sendMsg('/load', 2, "shader:04");
~visuals.sendMsg('/load', 2, "shader:RotStringsH");
~visuals.sendMsg('/alpha', 3, 1.0);
~visuals.sendMsg('/load', 3, "");
~visuals.sendMsg('/color/mfcc', 2, false);
~visuals.sendMsg('/alpha', 2, 1.0);
~visuals.sendMsg('/alpha', 4, 1.0);

(
{
	100.do { |i|
		var j = (i+1);
		~visuals.sendMsg('/alpha', 2, 1-(j/100.0));
		~visuals.sendMsg('/alpha', 3, 1-(j/100.0));
		~visuals.sendMsg('/alpha', 4, 1-(j/100.0));
		1.0.wait;
	};
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