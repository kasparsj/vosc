## Overview

Light commands:
- `/light`
- `/light/remove`

These are top-level parser commands (not targeted layer resources).

## `/light`

Create or replace a light by id.

Arguments:
- `id` (string|int)
- `x`
- `y`
- `z`

Example:
```supercollider
~visuals.sendMsg('/light', 0, 500, 0, 500);
~visuals.sendMsg('/light', 'key', 250, 300, 400);
```

## `/light/remove`

Remove light by id.

Arguments:
- `id` (string|int)

Example:
```supercollider
~visuals.sendMsg('/light/remove', 0);
```
