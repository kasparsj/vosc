(
// microphone analysis synth
SynthDef(\soundin, {|out=0, in=0, rate=60|
	var input, amp, chain, onset, loudness, mfcc, trig;
  input = SoundIn.ar(in);
	chain = FFT(LocalBuf(1024), input);
	onset = Onsets.kr(chain);
	amp = Amplitude.kr(input);
  loudness = Loudness.kr(chain);
	mfcc = MFCC.kr(chain);
  trig = Impulse.kr(rate);
  // forward data to \visuals_sender OSCdef
  SendReply.kr(trig, '/visuals', [in, onset, amp, loudness] ++ mfcc);
//	Out.ar(out, input);
}).add;

// audio file analysis synth
SynthDef(\buf, {|out=0, in=0, rate=60, bufnum|
	var input, amp, chain, onset, loudness, mfcc, trig;
	input = PlayBuf.ar(1, bufnum, BufRateScale.kr(bufnum), loop: 1);
	chain = FFT(LocalBuf(1024), input);
	onset = Onsets.kr(chain);
	amp = Amplitude.kr(input);
  loudness = Loudness.kr(chain);
	mfcc = MFCC.kr(chain);
  trig = Impulse.kr(rate);
  // forward data to \visuals_sender OSCdef
  SendReply.kr(trig, '/visuals', [in, onset, amp, loudness] ++ mfcc);
	Out.ar(out, Splay.ar(input));
}).add;
)

(
// create visuals sender
~visuals = NetAddr("127.0.0.1", 33333);

// set to true if you want to send fake tidal onset events to OSC_VJ (e.g. when working with a TidalCycles audio recording file)
~fakeDirtPlay = false;

~synths = [];

~file = nil;
// uncomment to play a file instead of Microphone
// ~file = "/Users/kasparsj/Music/theme1.wav";

if (~file.isNil, {
	~numChannels = 1;
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
// sends OSC messages with sound analysis data to OSC_VJ
// this includes amplitude, loudness, onset, mfcc
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
// start the synths
var synthName = if (~file.isNil, { \soundin }, { \buf });
~numChannels.do {|i|
	var bufnum = if (~file.isNil, { nil }, { ~buffers[i].bufnum; });
	~synths = ~synths.add(Synth(synthName, [in: i, rate: 60, bufnum: bufnum]));
};
)

(
// cleanup - kill synths
~synths.do { |synth|
	synth.free;
}
)