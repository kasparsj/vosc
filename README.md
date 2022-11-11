# osc_vj
2D/3D OSC VJing software built on openFrameworks. This is work in progress!
# Command overview

## Setup
- `/sound/stream` [Setup sound stream](#sound-stream)
- `/sound/data` [Update sound data from SC](#sound-data)
- `/layers` [Setup layers](#setup-layers)
- `/layers/layout` [Change layout](#change-layout)

## Camera
- `/cam` [Use camera](#use-camera)
- `/cam/pos` [Camera position](#camera-position)
- `/cam/look` [Camera look at](#camera-look)
- `/cam/set` [Set camera property](#camera-set)

## Textures
- `/tex` [Load texture](#texture-load)
- `/tex/reload` [Reload texture](#texture-reload)
- `/tex/unload` [Unload texture](#texture-unload)
- `/tex/choose` [Choose texture at random](#texture-choose)
- `/tex/clear` [Clear texture](#texture-clear)
- `/tex/set` [Set texture property](#texture-set)
- `/tex/size` [Set texture size](#texture-size)
- `/tex/color` [Seek texture color](#texture-color)
- `/tex/tint` [Seek texture tint](#texture-tint)
- `/tex/speed` [Seek texture playback speed](#texture-speed)
- `/tex/seek` [Seek texture playback position](#texture-seek)
- `/tex/var` [Set texture variable](#texture-variable)
- `/tex/uniform` [Set texture variable](#texture-variable)
- `/tex/fbo` [Configure texture FBO](#texture-fbo)
- `/tex/looper` [Enable texture looper](#texture-looper)

## Geometry
- `/geom` [Load geometry](#geometry)
- `/geom/set` [Set geometry property](#geometry-set)

## Shaders
- `/shader` [Load layer shader](#shader)
- `/shader/uniform` [Set layer shader uniform](#shader-uniform)
- `/shader/var` [Set layer shader uniform](#shader-uniform)
- `/shader/texture` [Set layer shader texture](#shader-texture)
- `/shader/set` [Set layer shader property](#shader-set)

## Layers
- `/layer/pos` [Set layer position](#layer-position)
- `/layer/size` [Set layer size](#layer-size)
- `/layer/scale` [Set layer scale](#layer-scale)
- `/layer/rot` [Set layer rotation](#layer-rotation)
- `/layer/rot/point` [Set layer rotation point](#layer-rotation-point)
- `/layer/rot/speed` [Set layer rotation speed](#layer-rotation-speed)
- `/layer/reset` [Reset layer](#layer-reset)
- `/layer/data` [Set layer data source](#layer-data)
- `/layer/color` [Set layer color](#layer-color)
- `/layer/tint` [Set layer tint](#layer-tint)
- `/layer/alpha` [Set layer alpha](#layer-alpha)
- `/layer/bri` [Set layer brightness](#layer-brightness)
- `/layer/delay` [Set layer delay](#layer-delay)

## Materials
- `/mat/diffuse` [Set material diffuse color](#diffuse)
- `/mat/ambient` [Set material ambient color](#ambient)
- `/mat/specular` [Set material specular color](#specular)
- `/mat/emissive` [Set material emissive color](#emissive)
- `/mat/shininess` [Set material shininess](#shininess)

# Command reference

## Setup

### Sound Stream

`/sound/stream`

Arguments:
- **sound** (int) sound index
- numChannels (int)
- bufferSize (int)

Examples:
```supercollider
~visuals.sendMsg('/sound/stream', 0); // setup OF sound listener
~visuals.sendMsg('/sound/stream', 0, 2); // setup OF sound listener with 2 channels
~visuals.sendMsg('/sound/stream', 0, 0); // close OF sound listener
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

## Camera

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
~visuals.sendMsg('/cam/pos', 0, 0, -3000, 10); // zoom out camera over 10 seconds
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
~visuals.sendMsg('/cam/set', "autoDistamce", false); // only when camera is set to "easy"
~visuals.sendMsg('/cam/set', "movementMaxSpeed", 1); // only when camera is set to "fp"
```

## Geometry

### Geometry load

`/geom`

Arguments:
- **target** (int|string) layer index or wildcard or shared geometry name, e.g. "*"
- **source** (string) model file or [primitive](#primitive)

Examples:
```supercollider
~visuals.sendMsg('/geom', 0, "plane"); // set first layer geometry to the default plane
~visuals.sendMsg('/geom', 0, "box"); // set first layer geometry to box/cube
~visuals.sendMsg('/geom', 0, "sphere"); // set first layer geometry to sphere
```

## Texture

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
~visuals.sendMsg('/tex', 0, ""); // unload first layer texture
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
- **layer** (int|string) layer index or wildcard, e.g. "*"
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
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **time** (float|string) time position as percentage
- duration (float)
- easing (int)

Examples:
```supercollider
~visuals.sendMsg('/tex/seek', 0, 0.5); // seek video to middle position
~visuals.sendMsg('/tex/seek', 0, 50); // seek video to middle position
~visuals.sendMsg('/tex/seek', 0, 0.25, 3); // animate video position to quarter of length in 3 seconds
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

## Layers

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

### Layer Rotation point

`/layer/rot/point`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **x** (float|string) x axis rotation point (0 to 1)
- **y** (float|string) y axis rotation point (0 to 1)

Examples:
```supercollider
~visuals.sendMsg('/layer/rot/point', 0, 0.5, 0.5); // set first layer rotation point to center
~visuals.sendMsg('/layer/rot/point', 0, "center"); // set first layer rotation point to center
```

### Layer Rotation speed

`/layer/rot/speed`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **x** (float)
- **y** (float)
- **z** (float)
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/layer/rot/speed', 0, 100, 0, 0); // set first layer rotation speed to 1 degree
~visuals.sendMsg('/layer/rot/speed', 0, 360, 0, 0, 2); // increase first layer rotation speed to 360 degrees over 2 seconds
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

### Layer Variable

`/layer/var`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- [**datasource**](#datasource) (string)

Examples:
```supercollider
~visuals.sendMsg('/layer/var', 0, "const"); // set first layer data source to constant 1
~visuals.sendMsg('/layer/var', 0, "const:23"); // set first layer data source to constant 23
~visuals.sendMsg('/layer/var', 0, "rand"); // set first layer data source to random
~visuals.sendMsg('/layer/var', 0, "rand:100"); // set first layer data source to random of 100
~visuals.sendMsg('/layer/var', 0, "noise"); // set first layer data source to perlin noise
~visuals.sendMsg('/layer/var', 0, "noise:50"); // set first layer data source to perlin noise of 50
~visuals.sendMsg('/layer/var', 0, "loud:0"); // set first layer data source to channel 1 loudness
~visuals.sendMsg('/layer/var', 0, "amp:1"); // set first layer data source to channel 2 loudness
// todo: perhaps allow setting loud & amp max values after second ":"
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

## Enumerations

### Layout
1. `"col"`
2. `"row"`
3. `"grid"`
4. `"stack"`

### Source
- `"color"`
- `"hpv"`
- `"image"`
- `"shader"`
- `"sketch"`
- `"video"`
- `"webcam"`

### primitive
- `"plane"`
- `"box"`
- `"sphere"`
- `"icosphere"`
- `"cylinder"`
- `"cone"`
- `"grass"`

## Datasource
- `"const"`
- `"rand"`
- `"noise"`
- `"sin"`
- `"amp"`
- `"loud"`
- `"onset"`
- `"tidal"`
