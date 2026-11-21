// Analyze microphone input or one channel of a local audio file.
(
var file = nil; // Set an absolute file path to analyze a file.
var channel = 0;
var synthName = if(file.isNil, { \voscInput }, { \voscFile });

if(file.notNil and: { File.exists(file).not }) {
    Error("Set file to an existing audio file").throw
};
if(~voscAnalysis.notNil) { Error("Stop the previous analysis first").throw };

if(s.serverRunning.not) { Error("Boot the SuperCollider audio server first").throw };

~voscAnalysis = Routine {
    var renderer = NetAddr("127.0.0.1", 33333);

    SynthDef(synthName, { |channel, bufnum|
        var signal = if(file.isNil, {
            SoundIn.ar(channel)
        }, {
            PlayBuf.ar(1, bufnum, BufRateScale.kr(bufnum), loop: 1)
        });
        var fft = FFT(LocalBuf(1024), signal);
        SendReply.kr(Impulse.kr(60), '/visuals', [
            Amplitude.kr(signal), Loudness.kr(fft), Onsets.kr(fft)
        ] ++ MFCC.kr(fft));
        if(file.notNil) { Out.ar(0, signal ! 2) };
    }).send(s);

    if(file.notNil) {
        ~voscBuffer = Buffer.readChannel(s, file, channels: [channel]);
    };
    s.sync;

    renderer.sendMsg('/input', "analysis");
    OSCdef(\voscAnalysis, { |msg|
        renderer.sendMsg('/input/data', "analysis", msg[3], msg[4], msg[5].asInteger, *msg[6..]);
    }, '/visuals', s.addr);

    ~voscSynth = Synth(synthName, [channel: channel, bufnum: ~voscBuffer.tryPerform(\bufnum) ? 0], target: s);
}.play(AppClock);
)

// On an existing layer, use immediate edits and map opacity to amplitude.
// NetAddr("127.0.0.1", 33333).sendMsg('/onset', 0);
// NetAddr("127.0.0.1", 33333).sendMsg('/layer/alpha', 0, "in_analysis");

// Stop the analysis and clear its last sample.
(
~voscAnalysis.stop;
~voscSynth.free;
~voscBuffer.free;
OSCdef.all[\voscAnalysis].free;
NetAddr("127.0.0.1", 33333).sendMsg('/input/data', "analysis", 0.0);
~voscSynth = nil;
~voscBuffer = nil;
~voscAnalysis = nil;
)
