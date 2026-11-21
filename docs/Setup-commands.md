## Overview

- `/input` setup or close microphone/stream input for a named input slot
- `/input/data` push analysis data to configured inputs (amp/loudness/onset/mfcc)
- `/input/set` set input properties (`maxAmp`, `maxLoud`)
- `/input/list` list available audio input devices
- `/layers` setup layers (optionally with layout)
- `/layers/reset` reset layer list and apply new layer setup
- `/layers/layout` change layout only

## Input commands

### `/input`

- `/input target` creates or reconfigures a single OSC-fed slot. It expands to
  `/input target "" 0 256`. Zero channels close any active audio stream on the
  slot. Reconfiguration preserves existing properties and samples.
- `/input target deviceName [numChannels [bufferSize]]` uses a device-name
  substring. With a supplied device name, channels default to 1 and buffer size
  defaults to 256. Positive channels open the first matching device.
  Nonpositive channels close the stream. Channels and buffer size must be
  integral OSC numbers in int32 range, and buffer size must be positive.
- `/input "*" deviceName numChannels bufferSize` reconfigures all existing slots.
  Wildcard setup requires all fields and creates no slots.

A target is a nonempty string or an OSC int32 or int64 in int32 range. The integer
`0` and the string `"0"` refer to the same slot. Only `"*"` acts as a wildcard.

```supercollider
~visuals.sendMsg('/input', 'external');
~visuals.sendMsg('/input', 'mic', 'Microphone');
~visuals.sendMsg('/input', 'mic', 'Microphone', 2, 512);
~visuals.sendMsg('/input', 'mic', 'Microphone', 0); // close
```

### `/input/data`

`/input/data target amp [loudness [onset [mfcc...]]]` replaces the sample on a
configured slot. `"*"` targets all configured slots. Unknown targets are ignored
and no slots are created. Sampling a slot leaves its configured ID type intact.

Omitted loudness and onset are zero. An omitted MFCC tail clears any previously
stored coefficients. The first accepted sample enables onset gating when the
gate is still in its automatic startup mode. Send `/onset 0` before testing
continuous mappings if you want edits to apply immediately. To supply onset,
also supply loudness. To supply MFCC,
supply both loudness and onset. The amplitude, loudness and MFCC values accept
finite numeric or boolean OSC arguments whose values fit in a float. Onset accepts
a boolean or integral numeric value in int32 range. Zero and false clear onset.

```supercollider
~visuals.sendMsg('/input/data', 'external', amp);
~visuals.sendMsg('/input/data', 'external', amp, loudness);
~visuals.sendMsg('/input/data', 'external', amp, loudness, onset.asInteger, *mfcc);
```

### `/input/set`

`/input/set target property value` sets `maxAmp` or `maxLoud` for an existing slot.
Supply exactly one finite OSC float. Unknown targets are ignored, and `"*"`
applies the property to all configured slots.

```supercollider
~visuals.sendMsg('/input/set', 'external', 'maxAmp', 0.5);
```

Recipe export and wrapped replay use explicit setup arguments with
[separate signatures and limits](Editable-scene-contract.md#wrapped-setup-arguments).
Some live configurations fall outside those limits, including negative integer
IDs or channel counts, buffers outside 64–8192, and nonpositive `maxAmp`.
`/input/data` is a live command and can run unwrapped during replay. Samples
remain external to the recipe.

### `/input/list`

Arguments: none

Behavior: prints available devices to log.

## Layer setup commands

### `/layers`

Live forms are `/layers`, `/layers count`, `/layers layoutName`, and
`/layers count layout`. The count accepts nonnegative integral OSC numbers
in int32 range, trimmed decimal strings parsed in full, and nonnegative OSC
char values. Layout accepts the exact names `stack`, `grid`, `col`, `row`, or
integral OSC numeric values 1–4 in that order. A lone numeric argument is a
count, and a lone layout name uses the default count of nine (`INITIAL_LAYERS`).
Omit layout to keep the current layout. Use `/layers/layout` to change the
layout alone.

Examples:
```supercollider
~visuals.sendMsg('/layers', 'grid');
~visuals.sendMsg('/layers', 8);
~visuals.sendMsg('/layers', 9, 'grid');
```

### `/layers/reset`

Accepts the same live forms and argument types as [`/layers`](#layers).

Discards the current layers and their owned resources, then creates fresh
layers. Omit the count to create nine layers, or pass zero to remove every
layer. Omit the layout to keep the current layout. For example,
`/layers/reset "grid"` rebuilds nine layers in grid layout. Wrapped replay
requires an explicit count and layout.

Named shared resources, configured inputs and samples, camera, lights,
render passes, window size, onset settings, Tidal notes, and pending OSC work
survive the rebuild. Pending commands can act on the replacement layers when
the gate next opens. Replay uses a separate whole-scene reset before rebuilding
the recipe's layers. See the [replay contract](Editable-scene-contract.md).

### `/layers/layout`

Takes exactly one layout, using the same names and numeric values as
[`/layers`](#layers). Relayout applies to the existing layers and can update
their transform defaults.

Examples:
```supercollider
~visuals.sendMsg('/layers/layout', 2);      // grid by enum value
~visuals.sendMsg('/layers/layout', 'grid');
```
