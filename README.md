# osc_vj
OSC VJing software built on openFrameworks

# Command overview

## Setup
- `/layers` [Setup layers](#setup-layers)
- `/layout` [Change layout](#change-layout)

## Content
- `/load` [Load content](#load)
- `/reload` [Reload content](#reload)
- `/unload` [Unload content](#unload)
- `/choose` [Choose content at random](#choose)

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

## Content

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
