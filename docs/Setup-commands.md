## Overview

- `/mic` [Setup microphone input](#setup-microphone)
- `/sound/data` [Update sound data from SC](#sound-data)
- `/layers` [Setup layers](#setup-layers)
- `/layers/layout` [Change layout](#change-layout)

## Reference

### Setup microphone

`/mic`

Arguments:
- **mic** (int) mic index
- **name** (string) device name
- numChannels (int)
- bufferSize (int)

Examples:
```supercollider
~visuals.sendMsg('/mic', 0, "Microphone"); // setup microphone (must have "Microphone" in device name)
~visuals.sendMsg('/mic', 0, "Microphone", 2); // setup microphone with 2 channels
~visuals.sendMsg('/mic', 0, "Microphone", 0); // close microphone
```

### Sound Data

`/sound/data`

Arguments:
- **sound** (int) sound index
- amplitude (float)
- loudness (float)
- onset (int)
- mfcc (float)

Examples:
```supercollider
OSCdef(\visuals_sender, {|msg|
  var data = msg[3..];
  var in = data[0];
  var onset = data[1];
  var amp = data[2];
  var loudness = data[3];
  var mfcc = data[4..];
  ~visuals.sendMsg('/sound/data', in, amp, loudness, onset.asInteger, *mfcc);
}, '/visuals');
```

### Setup layers

`/layers`

Arguments:
- **numLayers** (int)
- [layout](#layout) (int|string)

Examples:
```supercollider
~visuals.sendMsg('/layers', 8); // initializes 8 layers with stack layout
~visuals.sendMsg('/layers', 9, "grid"); // initializes 9 layers with grid layout
9.do { |i|
	~visuals.sendMsg('/tex', i, "black_hole.jpeg");
};
```

### Change layout

`/layers/layout`

Arguments:
- [**layout**](#layout) (int|string)
- args (int)

Examples:
```supercollider
~visuals.sendMsg('/layers/layout', 3); // change layout to grid
~visuals.sendMsg('/layers/layout', "grid"); // change layout to grid
```