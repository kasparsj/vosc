## Overview

Layer commands are targeted resource commands.

First argument is `target`:
- `int` or numeric string: layer index
- `"*"`, `"x"`, `"a"`: all layers

Note: non-numeric string names are not supported for `/layer*` commands.

Supported commands:
- `/layer/visible`
- `/layer/solo`
- `/layer/pos`
- `/layer/size`
- `/layer/scale`
- `/layer/rot`
- `/layer/pivot`
- `/layer/align`
- `/layer/tint`
- `/layer/alpha`
- `/layer/bri`
- `/layer/delay`
- `/layer/behaviour`
- `/layer/reset`

## Transform and visibility

### `/layer/visible`
Arguments: `target`, `bool`

### `/layer/solo`
Arguments: `target`

Behavior: hides all other layers and keeps target visible.

### `/layer/pos`
Arguments: `target`, position payload (`x,y[,z]`, const/expression)

### `/layer/size`
Arguments: `target`, size payload (`w,h[,d]`)

### `/layer/scale`
Arguments: `target`, scale payload (`x[,y[,z]]`)

### `/layer/rot`
Arguments: `target`, rotation axis-angle vector payload

### `/layer/pivot`
Arguments: `target`, pivot payload (`x,y[,z]` in normalized layer space)

### `/layer/align`
Arguments:
- `target`
- either symbolic alignment strings or numeric enum values

String mode:
- horizontal: `left`, `center`, `right`
- vertical: `top`, `center`, `bottom`
- if only one string is supplied, it is reused for both axes

Numeric mode:
- first value is `ofAlignHorz`
- optional second value is `ofAlignVert`

## Color and timing

### `/layer/tint`
Arguments: `target`, color payload

### `/layer/alpha`
Arguments: `target`, alpha payload

### `/layer/bri`
Arguments: `target`, brightness payload

### `/layer/delay`
Arguments: `target`, delay frames (int/float)

## Behavior and reset

### `/layer/behaviour`
Arguments: `target`, behaviour id (int)

Currently stored on layer but not actively consumed by render logic.

### `/layer/reset`
Arguments: `target`

Resets transform, tint/alpha/bri defaults, shader/geom/vars owned by layer.

## Examples

```supercollider
~visuals.sendMsg('/layer/solo', 0);
~visuals.sendMsg('/layer/tint', '*', 1, 0.8, 0.8);
~visuals.sendMsg('/layer/align', 2, 'center', 'bottom');
~visuals.sendMsg('/layer/delay', 2, 24);
~visuals.sendMsg('/layer/reset', '*');
```
