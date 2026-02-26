## Overview

- `/input` setup or close microphone/stream input for a named input slot
- `/input/data` push analysis data (amp/loudness/onset/mfcc)
- `/input/set` set input properties (`maxAmp`, `maxLoud`)
- `/input/list` list available audio input devices
- `/layers` setup layers (optionally with layout)
- `/layers/reset` reset layer list and apply new layer setup
- `/layers/layout` change layout only

## Input commands

### `/input`

Arguments:
- `target` (string|int) input slot id (for example `0`, `"mic"`)
- `deviceName` (string) substring matched against system input devices
- `numChannels` (int, optional, default `1`)
- `bufferSize` (int, optional, default `256`)

Behavior:
- if `numChannels > 0`: opens stream
- if `numChannels <= 0`: closes stream

Examples:
```supercollider
~visuals.sendMsg('/input', 'mic', 'Microphone');
~visuals.sendMsg('/input', 'mic', 'Microphone', 2, 512);
~visuals.sendMsg('/input', 'mic', 'Microphone', 0); // close
```

### `/input/data`

Arguments:
- `target` (string|int) input slot id
- `amp` (float)
- `loudness` (float)
- `onset` (int|bool)
- `mfcc...` (float, optional variadic)

Example:
```supercollider
~visuals.sendMsg('/input/data', 'mic', amp, loudness, onset.asInteger, *mfcc);
```

### `/input/set`

Arguments:
- `target` (string|int) input slot id
- `property` (string) `maxAmp` | `maxLoud`
- `value` (float)

Example:
```supercollider
~visuals.sendMsg('/input/set', 'mic', 'maxAmp', 0.5);
```

### `/input/list`

Arguments: none

Behavior: prints available devices to log.

## Layer setup commands

### `/layers`

Arguments:
- `numLayers` (int, optional; default falls back to app `INITIAL_LAYERS`)
- `layout` (int|string, optional) see [Layouts](Constants.md#layouts)

Examples:
```supercollider
~visuals.sendMsg('/layers', 8);
~visuals.sendMsg('/layers', 9, 'grid');
```

### `/layers/reset`

Arguments:
- `numLayers` (int, optional)
- `layout` (int|string, optional)

Behavior: clears and recreates layers, then applies payload.

### `/layers/layout`

Arguments:
- `layout` (int|string) see [Layouts](Constants.md#layouts)

Examples:
```supercollider
~visuals.sendMsg('/layers/layout', 2);      // grid by enum value
~visuals.sendMsg('/layers/layout', 'grid');
```
