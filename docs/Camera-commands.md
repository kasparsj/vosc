## Overview

- `/cam` [Use camera](#use-camera)
- `/cam/pos` [Camera position](#camera-position)
- `/cam/look` [Camera look at](#camera-look)
- `/cam/orbit` [Orbit camera](#camera-orbit)
- `/cam/set` [Set camera property](#camera-set)

## Reference

### Use camera

`/cam`

Arguments:
- **camera** (string) switch camera

Examples:
```supercollider
~visuals.sendMsg('/cam', "default"); // enable default (ofCamera) camera
~visuals.sendMsg('/cam', ""); // disable camera
~visuals.sendMsg('/cam', "easy"); // enable easy (ofEasyCam) camera
```

### Camera position

`/cam/pos`

Arguments:
- **x** (float)
- **y** (float)
- **z** (float)
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/cam/pos', 1, 2, 100); // set camera position
~visuals.sendMsg('/cam/pos', 0, 0, -3000, 10); // zoom out camera over 10 seconds

// map camera distance to a sine variable oscillating between 400 and 500
~visuals.sendMsg('/var', "sine_osc", "sin");
~visuals.sendMsg('/var/range', "sine_osc", 400, 1500);
// map camera's z coordinate to sine_osc variable
~visuals.sendMsg('/cam/pos', 0, 0, "sine_osc");
```

### Camera look

`/cam/look`

Arguments:
- **x** (float)
- **y** (float)
- **z** (float)
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/cam/look', 0, 0, 0); // set camera to look at center of the scene
```

### Camera orbit

`/cam/orbit`

Arguments:
- **degrees** (float) degrees per second
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/cam/orbit', 20); // orbit camera 20 degrees per second
```

### Camera set

`/cam/set`

Arguments:
- **property** (string)
- **arg1** (float)
- arg2 (float)
- arg3 (float)

Examples:
```supercollider
~visuals.sendMsg('/cam/set', "nearClip", 0.1);
~visuals.sendMsg('/cam/set', "farClip", 10000);
~visuals.sendMsg('/cam/set', "globalPosition", 0, 0, -870);
~visuals.sendMsg('/cam/set', "distance", 1000); // only when camera is set to "easy"
~visuals.sendMsg('/cam/set', "autoDistance", false); // only when camera is set to "easy"
~visuals.sendMsg('/cam/set', "movementMaxSpeed", 1); // only when camera is set to "fp"
```