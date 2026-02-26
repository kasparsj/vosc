## Overview

Variable commands are targeted resource commands.

First argument is `target`:
- `int` or numeric string: layer index (layer-local variable)
- `"*"`, `"x"`, `"a"`: all layers (layer-local variable)
- non-numeric string: shared variable name (global/shared pool)

Supported commands:
- `/var`
- `/var/colors`
- `/var/colors/scheme`

## `/var`

Set or update variable payload.

Two argument forms are used:
- layer target form (`target` is index or wildcard):
  - `target`
  - `name` (variable key)
  - value payload
- shared form (`target` is non-numeric string):
  - `target` (shared variable name)
  - value payload

Accepted payload forms include:
- numeric/int/bool scalar
- multi-value vectors (`vec2`, `vec3`, color, mat4 sized payloads)
- expression strings
- JSON vector strings
- blob payloads

Examples:
```supercollider
~visuals.sendMsg('/var', 0, 'speed', 1.5);
~visuals.sendMsg('/var', 0, 'pos', 100, 200, 0);
~visuals.sendMsg('/var', 'sharedTimeScale', 'sin', 0.2);
```

## `/var/colors`

Set color array variable.

Two forms:
- layer target form: `target`, `name`, color payload
- shared form: `target`, color payload

## `/var/colors/scheme`

Generate color array from color-wheel scheme.

Two forms:
- layer target form: `target`, `name`, `schemeName`, `primaryColor`, `numColors?`
- shared form: `target`, `schemeName`, `primaryColor`, `numColors?`

Note: color-specific command behavior is fully implemented for shared variables (name-target mode).

## Datasource names

Common datasource keywords used in expressions/var payloads:
- `time`, `const`, `rand`, `noise`, `sin`, `cos`, `mic`, `amp`, `loud`, `onset`, `tidal`

See [Constants](Constants.md#datasources).
