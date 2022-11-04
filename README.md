# osc_vj
OSC VJing software built on openFrameworks
# Command overview

## Setup
- `/sounds` [Setup sounds](#setup-sounds)
- `/layers` [Setup layers](#setup-layers)
- `/layout` [Change layout](#change-layout)

## Content
- `/load` [Load content](#load)
- `/reload` [Reload content](#reload)
- `/unload` [Unload content](#unload)
- `/choose` [Choose content at random](#choose)

## Geometry
- `/geom` [Set layer geometry](#geom)

## Data
- `/data` [Set layer data source](#data)

## Transform
- `/pos` [Set layer position](#position)
- `/scale` [Set layer scale](#scale)
- `/size` [Set layer size](#size)
- `/rot` [Set layer rotation](#rotation)
- `/rot/point` [Set layer rotation point](#rotation-point)
- `/rot/speed` [Set layer rotation speed](#rotation-speed)
- `/reset` [Reset layer](#reset)

## Color
- `/tint` [Set layer tint](#tint)
- `/alpha` [Set layer alpha](#alpha)
- `/bri` [Set layer brightness](#brightness)

## Time
- `/delay` [Set layer delay](#delay)
- `/looper` [Activate layer looper](#looper)
- `/speed` [Set layer speed](#speed)
- `/seek` [Seek video layer](#seek)

## Camera
- `/cam` [Use camera](#use-camera)
- `/cam/pos` [Camera position](#camera-position)

# Command reference

## Setup

### Setup sounds

`/sounds`

Arguments:
- **numChannels** (int)

Examples:
```supercollider
~visuals.sendMsg('/sounds', 1); // initializes 1 channel audio data listener
~visuals.sendMsg('/sounds', 0); // deletes all audio data listeners
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
	~visuals.sendMsg('/load', i, "black_hole.jpeg");
};
```

### Change layout

`/layout`

Arguments:
- [**layout**](#layout) (int|string)
- numLayers (int)

Examples:
```supercollider
~visuals.sendMsg('/layout', 3); // change layout to grid
~visuals.sendMsg('/layout', "grid"); // change layout to grid
```

## Content

### Load

`/load`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- content (string) relative or absolute path or URL

Examples:
```supercollider
~visuals.sendMsg('/load', 0, "0xff0000"); // load red color into first layer
~visuals.sendMsg('/load', 0, "black_hole.jpeg"); // load image into first layer
~visuals.sendMsg('/load', 0, "OscCircle.frag"); // load a fragment shader into first layer
~visuals.sendMsg('/load', 0, "NoisySpirals"); // load a c++ sketch into first layer
~visuals.sendMsg('/load', 0, "DSC_0081.mov"); // load a movie into first layer
~visuals.sendMsg('/load', 0, "bbb_export.hpv"); // load high performance video into first layer
~visuals.sendMsg('/load', 0, "webcam:0"); // load first webcam device into first layer
~visuals.sendMsg('/load', 0, ""); // unload first layer content
```

### Reload

`/reload`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"

Examples:
```supercollider
~visuals.sendMsg('/reload', "*"); // reload all layers
```

### Unload

`/unload`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"

Examples:
```supercollider
~visuals.sendMsg('/unload', "*"); // unload all layers
// same as:
~visuals.sendMsg('/load', "*", ""); // unload all layers
```

### Choose

`/choose`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- [source](#source) (string) source type

Examples:
```supercollider
~visuals.sendMsg('/choose', 0, "3d"); // load random 3d mesh
~visuals.sendMsg('/choose', 0, "video"); // load random video from library
~visuals.sendMsg('/choose', 0, "image"); // load random image from library
```

## Transform

### Position

`/pos`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **x** (float)
- **y** (float)
- z (float)
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/pos', 0, 100, 150); // offset first layer by x:100, y:150
~visuals.sendMsg('/pos', 0, 100, 150, 200); // offset first layer by x:100, y:150, z:200
~visuals.sendMsg('/pos', 0, 100, 150, 0, 1); // animate first layer position to x:100, y:150, over 1 second
```

### Scale

`/scale`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **x** (float) width scale as percentage
- **y** (float) height scale as percentage
- **z** (float) depth scale as percentage
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/scale', 0, 2); // scale first layer 2x
~visuals.sendMsg('/scale', 0, 2, 1); // scale width of first layer 2x
~visuals.sendMsg('/scale', 0, 2, 3, 1, 3); // scale width (2x) and height (3x) of first layer in 3 seconds tween
~visuals.sendMsg('/scale', 0, -1, -1, 1); // flip first layer both on x and y axis
```

### Size

`/size`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **w** (float) width
- **h** (float) height
- d (float) depth
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/size', 0, 100, 150); // resize first layer to w:100, h:150
~visuals.sendMsg('/size', 0, 100, 150, 200); // offset first layer by w:100, h:150, d:200
~visuals.sendMsg('/size', 0, 100, 150, 0, 1); // animate first layer position to w:100, h:150, over 1 second
```

### Rotation

`/rot`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **x** (float) x axis rotation multiplier (usually 0 or 1)
- **y** (float) y axis rotation multiplier (usually 0 or 1)
- **z** (float) z axis rotation multiplier (usually 0 or 1)
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/rot', 0, 0, 0, 90); // set first layer rotation to 90 on the z axis
~visuals.sendMsg('/rot', 0, 0, 0, 360, 10); // animate first layer rotation to 360 on the x axis
```

### Rotation point

`/rot/point`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **x** (float|string) x axis rotation point (0 to 1)
- **y** (float|string) y axis rotation point (0 to 1)

Examples:
```supercollider
~visuals.sendMsg('/rot/point', 0, 0.5, 0.5); // set first layer rotation point to center
~visuals.sendMsg('/rot/point', 0, "center"); // set first layer rotation point to center
```

### Rotation speed

`/rot/speed`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **x** (float)
- **y** (float)
- **z** (float)
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/rot/speed', 0, 100, 0, 0); // set first layer rotation speed to 1 degree
~visuals.sendMsg('/rot/speed', 0, 360, 0, 0, 2); // increase first layer rotation speed to 360 degrees over 2 seconds
```

### Reset

`/reset`

Reset layer properties (position, size, speed)

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"

Examples:
```supercollider
~visuals.sendMsg('/reset', 0); // reset first layers
```

## Geometry

### Geom

`/geom`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- [**geom**](#geom) (string)

Examples:
```supercollider
~visuals.sendMsg('/geom', 0, "plane"); // set first layer geometry to the default plane
~visuals.sendMsg('/geom', 0, "box"); // set first layer geometry to box/cube
~visuals.sendMsg('/geom', 0, "sphere"); // set first layer geometry to sphere
```

## Data

### Data

`/data`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- [**datasource**](#datasource) (string)

Examples:
```supercollider
~visuals.sendMsg('/data', 0, "const"); // set first layer data source to constant 1
~visuals.sendMsg('/data', 0, "const:23"); // set first layer data source to constant 23
~visuals.sendMsg('/data', 0, "rand"); // set first layer data source to random
~visuals.sendMsg('/data', 0, "rand:100"); // set first layer data source to random of 100
~visuals.sendMsg('/data', 0, "noise"); // set first layer data source to perlin noise
~visuals.sendMsg('/data', 0, "noise:50"); // set first layer data source to perlin noise of 50
~visuals.sendMsg('/data', 0, "loud:0"); // set first layer data source to channel 1 loudness
~visuals.sendMsg('/data', 0, "amp:1"); // set first layer data source to channel 2 loudness
// todo: perhaps allow setting loud & amp max values after second ":"
```

## Color

### Tint

`/tint`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **r, g, b** or **function** (float, float, float or string)

Examples:
```supercollider
~visuals.sendMsg('/tint', 0, 1.0, 0, 0); // tint first layer red
~visuals.sendMsg('/tint', 0, 255, 0, 0); // tint first layer red
~visuals.sendMsg('/tint', 0, "rand"); // tint first layer to random color
~visuals.sendMsg('/tint', 0, "mfcc"); // map first layer tint to mfcc data as color
~visuals.sendMsg('/tint', 0, "lerp", 0.1, 255, 0, 0, 0, 0, 255); // ting first layer to 10% between red and blue
```

### Alpha

`/alpha`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **alpha** (float)
- duration (float)
- easing (string)

Examples:
```supercollider
~visuals.sendMsg('/alpha', 0, 1.0); // set first layer opacity to 100%
~visuals.sendMsg('/alpha', 0, 100); // set first layer opacity to 100%
~visuals.sendMsg('/alpha', 0, 0.5, 2); // animate first layer opacity to 50% over 2 seconds
~visuals.sendMsg('/alpha', 0, 50, 2); // animate first layer opacity to 50% over 2 seconds
~visuals.sendMsg('/alpha', 0, 10, 2, "expo"); // animate first layer opacity to 10% over 2 seconds with exponential easing
```

### Brightness

`/bri`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **bri** (float)
- duration (float)
- easing (string)

Examples:
```supercollider
~visuals.sendMsg('/bri', 0, 1.0); // set first layer brightness to 100%
~visuals.sendMsg('/bri', 0, 100); // set first layer brightness to 100%
~visuals.sendMsg('/bri', 0, 0.5, 2); // animate first layer brightness to 50% over 2 seconds
~visuals.sendMsg('/bri', 0, 50, 2); // animate first layer brightness to 50% over 2 seconds
~visuals.sendMsg('/bri', 0, 10, 2, "expo"); // animate first layer brightness to 10% over 2 seconds with exponential easing
```

## Time

### Delay

`/delay`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **delay** (int) number of frames

Examples:
```supercollider
~visuals.sendMsg('/delay', 0, 60); // set layer 0 delay to 60 frames (1 second)
```

### Looper

`/looper`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **duration** (float) seconds
- fps (int) fps
- speed (float) speed

Examples:
```supercollider
~visuals.sendMsg('/looper', 0, 10, 30, 2); // enable looper 10 seconds durations, 30 fps, double speed
~visuals.sendMsg('/looper', 0, 0); // disable looper
```

### Speed

`/speed`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **speed** (float)
 
Examples:
```supercollider
~visuals.sendMsg('/speed', 0, 2); // speed up 2x first layer
~visuals.sendMsg('/speed', 0, 0.5); // slow down 2x first layer
~visuals.sendMsg('/speed', 0, 100); // speed up 100x first layer
~visuals.sendMsg('/speed', "*", 10); // speed up 10x all layers
```

### Seek

`/seek`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **time** (float|string) time position as percentage
- duration (float)
- easing (int)

Examples:
```supercollider
~visuals.sendMsg('/seek', 0, 0.5); // seek video to middle position
~visuals.sendMsg('/seek', 0, 50); // seek video to middle position
~visuals.sendMsg('/seek', 0, 0.25, 3); // animate video position to quarter of length in 3 seconds
```

## Camera

### Use camera

`/cam`

Arguments:
- **enabled** (bool) enable/disable camera

Examples:
```supercollider
~visuals.sendMsg('/cam', 1); // enable camera
~visuals.sendMsg('/cam', 0); // disable camera
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

### geom
- `"plane"`
- `"box"`
- `"sphere"`
- `"icosphere"`
- `"cylinder"`
- `"cone"`

## Datasource
- `"const"`
- `"rand"`
- `"noise"`
- `"sin"`
- `"amp"`
- `"loud"`
- `"onset"`
- `"tidal"`
