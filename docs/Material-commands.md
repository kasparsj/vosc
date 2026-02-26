## Overview

Material commands are targeted resource commands.

First argument is `target`:
- `int` or numeric string: layer index
- `"*"`, `"x"`, `"a"`: all layers

Note: non-numeric string names are not supported for `/mat*` commands.

Supported commands:
- `/mat/diffuse`
- `/mat/ambient`
- `/mat/specular`
- `/mat/emissive`
- `/mat/shininess`

## Commands

### `/mat/diffuse`
Arguments: `target`, color payload

### `/mat/ambient`
Arguments: `target`, color payload

### `/mat/specular`
Arguments: `target`, color payload

### `/mat/emissive`
Arguments: `target`, color payload

### `/mat/shininess`
Arguments: `target`, float payload

Example:
```supercollider
~visuals.sendMsg('/mat/diffuse', 0, 1.0, 0.5, 0.4, 1.0);
~visuals.sendMsg('/mat/specular', 0, 1.0, 1.0, 1.0, 1.0);
~visuals.sendMsg('/mat/shininess', 0, 32.0);
```
