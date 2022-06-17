# osc_vj
OSC VJing software built on openFrameworks

# Command overview

## Setup
- `/layers` [Setup layers](#setup-layers)
- `/layout` [Change layout](#change-layout)

## Layer
- `/load` [Load content](#load)
- `/reload` [Reload content](#reload)
- `/unload` [Unload content](#unload)
- `/choose` [Choose content at random](#choose)
- `/pos` [Set layer position](#position)
- `/size` [Set layer size](#size)
- `/reset` [Reset layer](#reset)

## Time
- `/speed` [Set layer speed](#speed)
- `/seek` [Seek video layer](#seek)

# Command reference

## Setup

### Setup layers

`"/layers"`

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

`"/layout"`

Arguments:
- [**layout**](#layout) (int|string)
- numLayers (int)

Examples:
```supercollider
~visuals.sendMsg('/layout', 3); // change layout to grid
~visuals.sendMsg('/layout', "grid"); // change layout to grid
```

## Layer

### Load

`"/load"`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- content (string) relative or absolute path or URL

Examples:
```supercollider
~visuals.sendMsg('/load', 0, "black_hole.jpeg"); // load image into first layer
~visuals.sendMsg('/load', 0, "OscCircle.frag"); // load a fragment shader into first layer
~visuals.sendMsg('/load', 0, "NoisySpirals"); // load a c++ sketch into first layer
~visuals.sendMsg('/load', 0, "DSC_0081.mov"); // load a movie into first layer
~visuals.sendMsg('/load', 0, "bbb_export.hpv"); // load high performance video into first layer
~visuals.sendMsg('/load', 0, ""); // unload first layer content
```

### Reload

`"/reload"`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"

Examples:
```supercollider
~visuals.sendMsg('/reload', "*"); // reload all layers
```

### Unload

`"/unload"`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"

Examples:
```supercollider
~visuals.sendMsg('/unload', "*"); // unload all layers
// same as:
~visuals.sendMsg('/load', "*", ""); // unload all layers
```

### Choose

`"/choose"`

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- [source](#source) (string) source type

Examples:
```supercollider
~visuals.sendMsg('/choose', 0, "3d"); // load random 3d mesh
~visuals.sendMsg('/choose', 0, "video"); // load random video from library
~visuals.sendMsg('/choose', 0, "image"); // load random image from library
```

### Position

`"/pos"`
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

### Size

`"/size"`
Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **w** (float) width
- **h** (float) height
- d (float) depth
- duration (float)

Examples:
```supercollider
~visuals.sendMsg('/pos', 0, 100, 150); // resize first layer to w:100, h:150
~visuals.sendMsg('/pos', 0, 100, 150, 200); // offset first layer by w:100, h:150, d:200
~visuals.sendMsg('/pos', 0, 100, 150, 0, 1); // animate first layer position to w:100, h:150, over 1 second
```

### Reset

`"/reset"`

Reset layer properties (position, size, speed)

Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"

Examples:
```supercollider
~visuals.sendMsg('/reset', 0); // reset first layers
```

## Time

### Speed

`"/speed"`
Arguments:
- **layer** (int|string) layer index or wildcard, e.g. "*"
- **speed** (float)
- 
Examples:
```supercollider
~visuals.sendMsg('/speed', 0, 2); // speed up 2x first layer
~visuals.sendMsg('/speed', 0, 0.5); // slow down 2x first layer
~visuals.sendMsg('/speed', 0, 100); // speed up 100x first layer
~visuals.sendMsg('/speed', "*", 10); // speed up 10x all layers
```

### Seek

`"/seek"`
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

## Enumerations

### Layout
1. `"col"`
2. `"row"`
3. `"grid"`
4. `"stack"`

### Source
- `"3d"` [see 3d](#3d)
- `"hpv"`
- `"image"`
- `"shader"`
- `"sketch"`
- `"video"`

### 3d
- `"box"`
- `"sphere"`
- `"icosphere"`
- `"cylinder"`
- `"plane"`
- `"cone"`
