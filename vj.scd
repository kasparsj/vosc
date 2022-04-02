//m = ServerMeter.new(s, 8, 2);

(
~visuals = NetAddr("127.0.0.1", 33333);
~fakeDirtPlay = false;
~synths = [];
// ~file = "/Users/kasparsj/Music/ivars/tema1.wav";
// ~file = "/Users/kasparsj/Music/ivars/tema1_2.wav";
~file = "/Users/kasparsj/Music/ivars/tema1_1.wav";
if (~file.isNil.not, {
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
	~synths = ~synths.add(Synth(synthName, [in: i, rate: 120, bufnum: ~buffers[i].bufnum]));
};
)

(
~synths.do { |synth|
	synth.free;
}
)

~visuals.sendMsg('/visuals', 8);
~visuals.sendMsg('/layout', 3);
~visuals.sendMsg('/loud/thresh', 0);
~visuals.sendMsg('/loud/max', 32);
~visuals.sendMsg('/amp/thresh', 0.35);
~visuals.sendMsg('/amp/max', 0.1);
~visuals.sendMsg('/speed', 0.1);

~visuals.sendMsg('/shader/*/random');
~visuals.sendMsg('/shader/*/reload');
~visuals.sendMsg('/shader/*/noclear', true);
~visuals.sendMsg('/shader/*', "OscCircle"); // good
~visuals.sendMsg('/sketch/*', "NoisySpirals");
~visuals.sendMsg('/pos/*', 0, 0);
~visuals.sendMsg('/size/*', 960, 1080);
~visuals.sendMsg('/data/*', "amp");
~visuals.sendMsg('/data/*', "loud");
~visuals.sendMsg('/color/*', 256.rand, 256.rand, 256.rand);
~visuals.sendMsg('/video/*', "");
~visuals.sendMsg('/video/*/random');
~visuals.sendMsg('/video/*/pos/random');

~visuals.sendMsg('/color/0', 0.15, 0.75, 1.0); // blue
~visuals.sendMsg('/color/0', 1.0, 0.45, 0); // orange
~visuals.sendMsg('/color/0', 0.4615, 0.615, 1.0); // blue2
~visuals.sendMsg('/color/0', 1.0, 1.0, 0.25); // yellow
~visuals.sendMsg('/color/0', 0, 0, 0); // reset
~visuals.sendMsg('/color/0', 1.0, 0, 0); // red
~visuals.sendMsg('/color/0', 256.rand, 256.rand, 256.rand); // random
~visuals.sendMsg('/color/0/random'); // random

~visuals.sendMsg('/unload/*');
~visuals.sendMsg('/sketch/*/reset');

~visuals.sendMsg('/onset');
~visuals.sendMsg('/data/0', "const");
~visuals.sendMsg('/data/0', "rand");
~visuals.sendMsg('/data/0', "noise");
~visuals.sendMsg('/data/1f', "noise");
~visuals.sendMsg('/data/*', "loud:0");
~visuals.sendMsg('/shader/0', "AtmoScattering");
~visuals.sendMsg('/sketch/0', "WaveClock");
~visuals.sendMsg('/sketch/1', "NoisySpirals");
~visuals.sendMsg('/sketch/0/reset'); // good

/// PERFORMANCE

~visuals.sendMsg('/sketch/0/reset');

(
~visuals.sendMsg('/loud/max', 32);
~visuals.sendMsg('/data/0', "loud:0");
~visuals.sendMsg('/sketch/0', "NoisySpirals");
~visuals.sendMsg('/sketch/0/alpha', 1.0);
~visuals.sendMsg('/sketch/0/bri', 1.0);
~visuals.sendMsg('/color/0/mfcc', false);
)

(
~visuals.sendMsg('/loud/max', 32);
~visuals.sendMsg('/data/1', "loud:0");
~visuals.sendMsg('/sketch/1', "WaveClock");
~visuals.sendMsg('/sketch/1/alpha', 1.0);
~visuals.sendMsg('/sketch/1/bri', 1.0);
~visuals.sendMsg('/color/1/mfcc', true);
)

(
~visuals.sendMsg('/shader/0', "OscCircle");
~visuals.sendMsg('/loud/thresh', 0.7);
~visuals.sendMsg('/color/0', 1.0, 1.0, 1.0);
{
	100.do { |i|
		var j = (i+1);
		~visuals.sendMsg('/shader/0/alpha', j/100.0);
		~visuals.sendMsg('/sketch/0/bri', 1-(j/100.0));
		~visuals.sendMsg('/sketch/1/bri', 1-(j/100.0));
		0.3.wait;
	};
	~visuals.sendMsg('/loud/thresh', 0.6);
	"bri fade done".postln;
}.fork;
)

(
~visuals.sendMsg('/loud/thresh', 0.5);
{
	// lerp to blue
	100.do { |i|
		var j = (i+1);
		~visuals.sendMsg('/color/0/lerp', j/100.0, 1.0, 1.0, 1.0, 0.15, 0.75, 1.0);
		~visuals.sendMsg('/sketch/0/alpha', 1-(j/100.0));
		~visuals.sendMsg('/sketch/1/alpha', 1-(j/100.0));
		0.3.wait;
	};
	~visuals.sendMsg('/color/0', 0, 0, 0);
	~visuals.sendMsg('/sketch/0', "");
	~visuals.sendMsg('/sketch/1', "");
	"colorlerp+alpha done".postln;
}.fork;
)

(
{
	// lerp to orange
	100.do { |i|
		var j = (i+1);
		~visuals.sendMsg('/color/0/lerp', j/100.0, 0.15, 0.75, 1.0, 1.0, 0.45, 0); // orange
		// ~visuals.sendMsg('/color/0/lerp', j/100.0, 1.0, 1.0, 0.45, 0, 0.4615, 0.615, 1.0); // blue2
		// ~visuals.sendMsg('/color/0/lerp', j/100.0, 0.4615, 0.615, 1.0, 1.0, 1.0, 0.25); // yellow
		0.1.wait;
	};
	"colorlerp done".postln;
}.fork;
)

(
{
	//~visuals.sendMsg('/loud/thresh', 0);
	~visuals.sendMsg('/sketch/1', "Spiral");
	~visuals.sendMsg('/sketch/1/bri', 1.0);
	100.do { |i|
		var j = (i+1);
		~visuals.sendMsg('/sketch/1/alpha', (j/100.0));
		0.1.wait;
	};
}.fork;
)