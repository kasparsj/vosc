## Overview

Layer commands are targeted resource commands.

The first argument selects a layer by nonnegative OSC `int32` or by a string
containing only decimal digits within int32 range. The index must identify an
existing layer. The strings `"*"`, `"x"`, and `"a"` select all layers.

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
Live position accepts `target` followed by separate `x, y[, z]` components
(constants or expressions), or one JSON-array string containing exactly three
finite numbers in float32 range or valid expression strings. OSCVisuals encodes
a live Array as that string. Wrapped replay uses an OSC int32 layer index and
three flattened typed components.

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

Unloads the layer geometry, clears its variables, and resets its shader.
Position and size are sampled and kept as values. Visibility, speed, tint,
alpha, brightness, rotation, pivot, scale, and visibility/onset thresholds
return to their defaults.

## Examples

```supercollider
~visuals.sendMsg('/layer/solo', 0);
~visuals.sendMsg('/layer/tint', '*', 1, 0.8, 0.8);
~visuals.sendMsg('/layer/align', 2, 'center', 'bottom');
~visuals.sendMsg('/layer/delay', 2, 24);
~visuals.sendMsg('/layer/reset', '*');
```
