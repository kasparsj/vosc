## Overview

- `/geom` [Load geometry](#geometry-load)
- `/geom/set` [Set geometry property](#geometry-set)

## Reference

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
~visuals.sendMsg('/geom', 0, "penguin/penguin.dae"); // load penguin model into first layer
```

### Geometry set

`/geom/set`

Arguments:
- **target** (int|string) layer index or wildcard or shared geometry name, e.g. "*"
- **property** (string) property name
- **value** (int|bool) property value

Examples:
```supercollider
~visuals.sendMsg('/geom/set', 0, "drawInstanced", 100); // set first layer geometry to draw 100 instanced
~visuals.sendMsg('/geom/set', 0, "meshMode", 3); // set first layer geometry mesh mode to OF_PRIMITIVE_LINES
~visuals.sendMsg('/geom/set', 0, "drawWireframe", true); // set first layer geometry to drawWireframe
```