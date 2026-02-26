## Overview

- `/cam` enable/disable camera and choose camera type
- `/cam/reset` reset camera vars (`camPos`, `camLook`, `camOrbit`)
- `/cam/pos` set camera position
- `/cam/look` set look-at target
- `/cam/orbit` set orbit speed (easy cam only)
- `/cam/set` set camera properties

## Reference

### `/cam`

Arguments:
- `type` (string, optional)

Supported `type` values:
- `"easy"` -> `ofEasyCam`
- `"fp"` -> `ofxFirstPersonCamera`
- any non-empty string -> base `ofCamera`
- empty / omitted -> disable camera

Examples:
```supercollider
~visuals.sendMsg('/cam', 'easy');
~visuals.sendMsg('/cam', 'fp');
~visuals.sendMsg('/cam', 'default');
~visuals.sendMsg('/cam', ''); // disable
```

### `/cam/reset`

Arguments: none

Resets shared vars to defaults:
- `camPos = (0,0,-870)`
- `camLook = (0,0,0)`
- `camOrbit = 0`

### `/cam/pos`

Arguments:
- `x,y,z` (mixed; const or expression-driven var payload)

Example:
```supercollider
~visuals.sendMsg('/cam/pos', 0, 0, -1200);
```

### `/cam/look`

Arguments:
- `x,y,z` (mixed; const or expression-driven var payload)

Example:
```supercollider
~visuals.sendMsg('/cam/look', 0, 0, 0);
```

### `/cam/orbit`

Arguments:
- `degreesPerSecond` (float)

Notes:
- only meaningful for `easy` camera mode

Example:
```supercollider
~visuals.sendMsg('/cam/orbit', 20);
```

### `/cam/set`

Arguments:
- `property` (string)
- additional args depend on property

Supported properties:
- `nearClip` float
- `farClip` float
- `globalPosition` float float float
- `distance` float (`easy` cam)
- `autoDistance` bool (`easy` cam)
- `mouseInput` bool (`easy` cam)
- `movementMaxSpeed` float (`fp` cam)

Examples:
```supercollider
~visuals.sendMsg('/cam/set', 'nearClip', 0.1);
~visuals.sendMsg('/cam/set', 'farClip', 10000);
~visuals.sendMsg('/cam/set', 'globalPosition', 0, 0, -870);
~visuals.sendMsg('/cam/set', 'distance', 1000);
~visuals.sendMsg('/cam/set', 'autoDistance', false);
~visuals.sendMsg('/cam/set', 'mouseInput', false);
~visuals.sendMsg('/cam/set', 'movementMaxSpeed', 1);
```
