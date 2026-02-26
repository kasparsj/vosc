## Overview

- `/layer/visible` [Show/hide layer](#layer-visible)
- `/layer/solo` [Hide all other layers](#layer-solo)
- `/layer/pos` [Set layer position](#layer-position)
- `/layer/size` [Set layer size](#layer-size)
- `/layer/scale` [Set layer scale](#layer-scale)
- `/layer/rot` [Set layer rotation](#layer-rotation)
- `/layer/pivot` [Set layer pivot point](#layer-pivot-point)
- `/layer/reset` [Reset layer](#layer-reset)
- `/layer/color` [Set layer color](#layer-color)
- `/layer/tint` [Set layer tint](#layer-tint)
- `/layer/alpha` [Set layer alpha](#layer-alpha)
- `/layer/bri` [Set layer brightness](#layer-brightness)
- `/layer/delay` [Set layer delay](#layer-delay)

## Reference

### Layer visible

`/layer/visible`

Arguments:
- **layer** (int|string) layer index
- **visible** (bool) visibility

Examples:
```supercollider
~visuals.sendMsg('/layer/visible', 0, false); // hide first layer
~visuals.sendMsg('/layer/visible', 0, true); // show first layer
```

### Layer solo

`/layer/solo`

Arguments:
- **layer** (int|string) layer index

Examples:
```supercollider
~visuals.sendMsg('/layer/solo', 0); // hide all layers except first
```

### Layer Position

`/layer/pos`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **x** (float)
- **y** (float)
- z (float)
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/layer/pos', 0, 100, 150); // offset first layer by x:100, y:150
~visuals.sendMsg('/layer/pos', 0, 100, 150, 200); // offset first layer by x:100, y:150, z:200
~visuals.sendMsg('/layer/pos', 0, 100, 150, 0, 1); // animate first layer position to x:100, y:150, over 1 second
```

### Layer Size

`/layer/size`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **w** (float) width
- **h** (float) height
- d (float) depth
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/layer/size', 0, 100, 150); // resize first layer to w:100, h:150
~visuals.sendMsg('/layer/size', 0, 100, 150, 200); // offset first layer by w:100, h:150, d:200
~visuals.sendMsg('/layer/size', 0, 100, 150, 0, 1); // animate first layer position to w:100, h:150, over 1 second
```

### Layer Scale

`/layer/scale`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **x** (float) width scale as percentage
- **y** (float) height scale as percentage
- **z** (float) depth scale as percentage
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/layer/scale', 0, 2); // scale first layer 2x
~visuals.sendMsg('/layer/scale', 0, 2, 1); // scale width of first layer 2x
~visuals.sendMsg('/layer/scale', 0, 2, 3, 1, 3); // scale width (2x) and height (3x) of first layer in 3 seconds tween
~visuals.sendMsg('/layer/scale', 0, -1, -1, 1); // flip first layer both on x and y axis
```

### Layer Rotation

`/layer/rot`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **x** (float) x axis rotation multiplier (usually 0 or 1)
- **y** (float) y axis rotation multiplier (usually 0 or 1)
- **z** (float) z axis rotation multiplier (usually 0 or 1)
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/layer/rot', 0, 0, 0, 90); // set first layer rotation to 90 on the z axis
~visuals.sendMsg('/layer/rot', 0, 0, 0, 360, 10); // animate first layer rotation to 360 on the x axis
```

### Layer Pivot point

`/layer/pivot`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **x** (float|string) x axis pivot point (0 to 1)
- **y** (float|string) y axis pivot point (0 to 1)

Examples:
```supercollider
~visuals.sendMsg('/layer/pivot', 0, 0.5, 0.5); // set first layer pivot to center
~visuals.sendMsg('/layer/pivot', 0, "center"); // set first layer pivot to center
```

### Layer Reset

`/layer/reset`

Reset layer properties (position, size, speed)

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"

Examples:
```supercollider
~visuals.sendMsg('/layer/reset', 0); // reset first layers
```

### Layer Color

`/layer/color`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **r, g, b** or **function** (float, float, float or string)

Examples:
```supercollider
~visuals.sendMsg('/layer/color', 0, 1.0, 0, 0); // set first layer color to red
~visuals.sendMsg('/layer/color', 0, 255, 0, 0); // set first layer color to red
~visuals.sendMsg('/layer/color', 0, "rand"); // set first layer to random color
~visuals.sendMsg('/layer/color', 0, "mfcc"); // map first layer color to mfcc data as color
~visuals.sendMsg('/layer/color', 0, "lerp", 0.1, 255, 0, 0, 0, 0, 255); // set first layer color to 10% between red and blue
```

### Layer Tint

`/layer/tint`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **r, g, b** or **function** (float, float, float or string)

Examples:
```supercollider
~visuals.sendMsg('/layer/tint', 0, 1.0, 0, 0); // tint first layer red
~visuals.sendMsg('/layer/tint', 0, 255, 0, 0); // tint first layer red
~visuals.sendMsg('/layer/tint', 0, "rand"); // tint first layer to random color
~visuals.sendMsg('/layer/tint', 0, "mfcc"); // map first layer tint to mfcc data as color
~visuals.sendMsg('/layer/tint', 0, "lerp", 0.1, 255, 0, 0, 0, 0, 255); // tint first layer to 10% between red and blue
```

### Layer Alpha

`/layer/alpha`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **alpha** (float)
- duration (float)
- easing (string)

Examples:
```supercollider
~visuals.sendMsg('/layer/alpha', 0, 1.0); // set first layer opacity to 100%
~visuals.sendMsg('/layer/alpha', 0, 100); // set first layer opacity to 100%
~visuals.sendMsg('/layer/alpha', 0, 0.5, 2); // animate first layer opacity to 50% over 2 seconds
~visuals.sendMsg('/layer/alpha', 0, 50, 2); // animate first layer opacity to 50% over 2 seconds
~visuals.sendMsg('/layer/alpha', 0, 10, 2, "expo"); // animate first layer opacity to 10% over 2 seconds with exponential easing
```

### Layer Brightness

`/layer/bri`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **bri** (float)
- duration (float)
- easing (string)

Examples:
```supercollider
~visuals.sendMsg('/layer/bri', 0, 1.0); // set first layer brightness to 100%
~visuals.sendMsg('/layer/bri', 0, 100); // set first layer brightness to 100%
~visuals.sendMsg('/layer/bri', 0, 0.5, 2); // animate first layer brightness to 50% over 2 seconds
~visuals.sendMsg('/layer/bri', 0, 50, 2); // animate first layer brightness to 50% over 2 seconds
~visuals.sendMsg('/layer/bri', 0, 10, 2, "expo"); // animate first layer brightness to 10% over 2 seconds with exponential easing
```

### Layer Delay

`/layer/delay`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **delay** (int) number of frames

Examples:
```supercollider
~visuals.sendMsg('/layer/delay', 0, 60); // set layer 0 delay to 60 frames (1 second)
```