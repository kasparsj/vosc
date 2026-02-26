## Overview

- `/tex` [Load texture](#texture-load)
- `/tex/reload` [Reload texture](#texture-reload)
- `/tex/unload` [Unload texture](#texture-unload)
- `/tex/choose` [Choose texture at random](#texture-choose)
- `/tex/clear` [Clear texture](#texture-clear)
- `/tex/var` [Set texture variable](#texture-variable)
- `/tex/set` [Set texture property](#texture-set)
- `/tex/size` [Set texture size](#texture-size)
- `/tex/color` [Set texture color](#texture-color)
- `/tex/tint` [Set texture tint](#texture-tint)
- `/tex/speed` [Set texture playback speed](#texture-speed)
- `/tex/seek` [Seek texture playback position](#texture-seek)
- `/tex/fbo` [Configure texture FBO](#texture-fbo)
- `/tex/looper` [Enable texture looper](#texture-looper)

## Reference

### Texture Load

`/tex`

Arguments:
- **target** (int|string) layer index or wildcard or shared texture name, e.g. "*"
- **source** (string) relative or absolute path or URL

Examples:
```supercollider
~visuals.sendMsg('/tex', 0, "0xff0000"); // load red color into first layer
~visuals.sendMsg('/tex', 0, "black_hole.jpeg"); // load image into first layer
~visuals.sendMsg('/tex', 0, "OscCircle.frag"); // load a fragment shader into first layer
~visuals.sendMsg('/tex', 0, "NoisySpirals"); // load a c++ sketch into first layer
~visuals.sendMsg('/tex', 0, "DSC_0081.mov"); // load a movie into first layer
~visuals.sendMsg('/tex', 0, "bbb_export.hpv"); // load high performance video into first layer
~visuals.sendMsg('/tex', 0, "webcam:0"); // load first webcam device into first layer
~visuals.sendMsg('/tex', 0, "https://apod.nasa.gov/apod/astropix.html"); // load a website into first layer
~visuals.sendMsg('/tex', 0, ""); // unload first layer texture

~visuals.sendMsg('/layers/layout', "grid"); // change to grid layout
~visuals.sendMsg('/tex', "t_webcam", "webcam:0"); // load first webcam device into a shared texture called "t_webcam"
9.do { |i|
	~visuals.sendMsg('/tex', i, "t_webcam"); // load shared "t_webcam" into a layer
	~visuals.sendMsg('/layer/delay', i, i*13);
};
```

### Texture Reload

`/tex/reload`

Arguments:
- **target** (int|string) layer index or wildcard or shared texture name, e.g. "*"

Examples:
```supercollider
~visuals.sendMsg('/tex/reload', "*"); // reload all layers
```

### Texture Unload

`/tex/unload`

Arguments:
- **target** (int|string) layer index or wildcard or shared texture name, e.g. "*"

Examples:
```supercollider
~visuals.sendMsg('/tex/unload', "*"); // unload all layers
// same as:
~visuals.sendMsg('/tex', "*", ""); // unload all layers
```

### Texture Choose

`/tex/choose`

Arguments:
- **target** (int|string) layer index or wildcard or shared texture name, e.g. "*"
- [source](#source) (string) source type

Examples:
```supercollider
~visuals.sendMsg('/tex/choose', 0, "video"); // load random video from library
~visuals.sendMsg('/tex/choose', 0, "image"); // load random image from library
```

### Texture Clear

`/tex/clear`

Arguments:
- **target** (int|string) layer index or wildcard or shared texture name, e.g. "*"

Examples:
```supercollider
~visuals.sendMsg('/tex/clear', 0); // clear first layer texture
```

### Texture Set

`/tex/set`

Arguments:
- **target** (int|string) layer index or wildcard or shared texture name, e.g. "*"
- **property** (string)
- **arg1** (int|float)
- arg2 (int|float)
- arg3 (int|float)

Examples:
```supercollider
~visuals.sendMsg('/tex/set', 0, "numFrames", 180); // set texture history to 180 frames (see [layer delay](#layer-delay))
~visuals.sendMsg('/tex/set', 0, "noClear", 1); // enable noClear
~visuals.sendMsg('/tex/set', 0, "blendMode", 4); // set blendMode to multiply
~visuals.sendMsg('/tex/set', 0, "aspectRatio", 0); // disable aspect ratio lock
~visuals.sendMsg('/tex/set', 0, "textureWrap", 0x2901, 0x2901); // set texture wrap to GL_REPEAT
```

### Texture Size

`/tex/size`

Arguments:
- **target** (int|string) layer index or wildcard or shared texture name, e.g. "*"
- **x** (float)
- **y** (float)

Examples:
```supercollider
~visuals.sendMsg('/tex/size', 0, 100, 100); // set first layer texture size to 100x100
```

### Texture Color

`/tex/color`

Arguments:
- **target** (int|string) layer index or wildcard or shared texture name, e.g. "*"
- **r, g, b** or **function** (float, float, float or string)

Examples:
```supercollider
~visuals.sendMsg('/tex/color', 0, 1.0, 0, 0); // set first layer color to red
~visuals.sendMsg('/tex/color', 0, 255, 0, 0); // set first layer color to red
~visuals.sendMsg('/tex/color', 0, "rand"); // set first layer to random color
~visuals.sendMsg('/tex/color', 0, "mfcc"); // map first layer color to mfcc data as color
~visuals.sendMsg('/tex/color', 0, "lerp", 0.1, 255, 0, 0, 0, 0, 255); // set first layer color to 10% between red and blue
```

### Texture Tint

`/tex/tint`

Arguments:
- **target** (int|string) layer index or wildcard or shared texture name, e.g. "*"
- **r, g, b** or **function** (float, float, float or string)

Examples:
```supercollider
~visuals.sendMsg('/tex/tint', 0, 1.0, 0, 0); // tint first layer red
~visuals.sendMsg('/tex/tint', 0, 255, 0, 0); // tint first layer red
~visuals.sendMsg('/tex/tint', 0, "rand"); // tint first layer to random color
~visuals.sendMsg('/tex/tint', 0, "mfcc"); // map first layer tint to mfcc data as color
~visuals.sendMsg('/tex/tint', 0, "lerp", 0.1, 255, 0, 0, 0, 0, 255); // tint first layer to 10% between red and blue
```

### Texture Speed

`/tex/speed`

Arguments:
- **target** (int|string) layer index or wildcard or shared texture name, e.g. "*"
- **speed** (float)
 
Examples:
```supercollider
~visuals.sendMsg('/tex/speed', 0, 2); // speed up 2x first layer
~visuals.sendMsg('/tex/speed', 0, 0.5); // slow down 2x first layer
~visuals.sendMsg('/tex/speed', 0, 100); // speed up 100x first layer
~visuals.sendMsg('/tex/speed', "*", 10); // speed up 10x all layers
```

### Texture Seek

`/tex/seek`

Arguments:
- **target** (int|string) layer index or wildcard or shared texture name, e.g. "*"
- **time** (float|string) time position as percentage
- duration (float)
- easing (int)

Examples:
```supercollider
~visuals.sendMsg('/tex/seek', 0, 0.5); // seek video to middle position
~visuals.sendMsg('/tex/seek', 0, 50); // seek video to middle position
~visuals.sendMsg('/tex/seek', 0, 0.25, 3); // animate video position to quarter of length in 3 seconds
```

### Texture Variable

`/tex/var`

Arguments:
- **target** (int|string) layer index or wildcard or shared texture name, e.g. "*"
- **name** (string) variable name
- [**datasource**](#datasource) (string)

Examples:
```supercollider
~visuals.sendMsg('/tex/var', 0, "myVar", 20); // set first layer data source to constant 20
~visuals.sendMsg('/tex/var', 0, "myVar", "const", 23); // set first layer data source to constant 23
~visuals.sendMsg('/tex/var', 0, "myVar", "rand"); // set first layer data source to random
~visuals.sendMsg('/tex/var', 0, "myVar", "rand", 100); // set first layer data source to random of 100
~visuals.sendMsg('/tex/var', 0, "myVar", "noise"); // set first layer data source to perlin noise
~visuals.sendMsg('/tex/var', 0, "myVar", "noise", 50); // set first layer data source to perlin noise of 50
~visuals.sendMsg('/tex/var', 0, "myVar", "amp"); // set first layer data source to channel 1 amplitude
~visuals.sendMsg('/tex/var', 0, "myVar", "loud", 0); // set first layer data source to channel 1 loudness
~visuals.sendMsg('/tex/var', 0, "myVar", "amp", 1); // set first layer data source to channel 2 amplitude
```

### Texture Looper

`/tex/looper`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **duration** (float) seconds
- fps (int) fps
- speed (float) speed

Examples:
```supercollider
~visuals.sendMsg('/tex/looper', 0, 10, 30, 2); // enable looper 10 seconds durations, 30 fps, double speed
~visuals.sendMsg('/tex/looper', 0, 0); // disable looper
```

