## Overview

Geometry commands are targeted resource commands. First argument is `target`:
- `int` or numeric string: layer index
- `"*"`, `"x"`, `"a"`: all layers
- non-numeric string: shared geometry name

Supported commands:
- `/geom`
- `/geom/choose`
- `/geom/set`

## `/geom`

Load geometry.

Arguments:
- `target`
- `source` (string)
- optional primitive/model args

Supported primitives (see [Primitives](Constants.md#primitives)):
- `plane`, `box`, `sphere`, `icosphere`, `cylinder`, `cone`, `grass`, `quad`

If `source` is not a primitive, runtime attempts model load from:
- absolute path
- `data/<source>`
- `data/models/<source>`

Examples:
```supercollider
~visuals.sendMsg('/geom', 0, 'plane');
~visuals.sendMsg('/geom', 0, 'box', 200, 200, 200);
~visuals.sendMsg('/geom', 0, 'penguin/penguin.dae');
~visuals.sendMsg('/geom', 'sharedMesh', 'sphere');
~visuals.sendMsg('/geom', 1, 'sharedMesh');
```

## `/geom/choose`

Choose random primitive and load it.

Arguments:
- `target`
- optional primitive args

## `/geom/set`

Set geometry draw properties.

Arguments:
- `target`
- `property` (string)
- property value

Supported properties:
- `drawInstanced` (int)
- `meshMode` (int, `ofPrimitiveMode`)
- `drawWireframe` (bool)

Example:
```supercollider
~visuals.sendMsg('/geom/set', 0, 'drawWireframe', true);
~visuals.sendMsg('/geom/set', 0, 'drawInstanced', 100);
```

## Recognized but not implemented

The runtime currently recognizes these geometry subcommands, but handlers are placeholders (`todo`) and do not apply transformations yet:
- `/geom/pos`
- `/geom/rot`
- `/geom/scale`
- `/geom/color`
- `/geom/boxes`
