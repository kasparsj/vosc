## Overview

Shader commands are targeted resource commands. First argument is `target`:
- `int` or numeric string: layer index
- `"*"`, `"x"`, `"a"`: all layers
- string without a parseable integer prefix: shared shader name

The string selector accepts integer prefixes. For example, `"1fx"` selects
layer 1.

Supported commands:
- `/shader`
- `/shader/set`
- `/shader/var` and `/shader/uniform`
- `/shader/texture` and `/shader/tex`
- `/shader/buffer` and `/shader/buf`
- `/shader/reset`

## `/shader`

Load shader.

Arguments:
- `target`
- `source` (string)

`source` can be:
- raw fragment source with a `void main(...)` entry point (whitespace allowed)
- local shader file/path (with optional paired `.vert`/`.geom`)
- shadertoy URL (`https://www.shadertoy.com/view/...`)

Explicit replacement compiles and validates a candidate before replacing the
active program. Failed source/file compilation or linking keeps the working
program and its configured variables/textures. The file watcher also validates
the candidate before replacing the active program. Failed watched saves log the
file path and compiler or linker diagnostics while the previous program keeps
rendering. A corrected save takes effect automatically. The
[local engine API](Engine-api.md) returns request-correlated diagnostics for
agent-authored fragment source.

Examples:
```supercollider
~visuals.sendMsg('/shader', 0, 'shaders/Water.frag');
~visuals.sendMsg('/shader', 0, 'https://www.shadertoy.com/view/MtSBDc');
~visuals.sendMsg('/shader', 'sharedFx', 'post/Chromatic');
~visuals.sendMsg('/shader', 2, 'sharedFx');
```

## `/shader/set`

Set shader properties (currently geometry-shader output/input configuration only).

Arguments:
- `target`
- `property`
- value

Supported properties:
- `geomInputType`
- `geomOutputType`
- `geomOutputCount`

## `/shader/var` and `/shader/uniform`

Set uniform variable value.

Arguments:
- `target`
- `name`
- value payload

For either live address, each selected layer expands a bare numeric OSC value
to the current width of an active, non-array vector uniform. A `vec2/3/4`
accepts a finite OSC float32 or an OSC int32 converted to float32. An `ivec2/3/4`
accepts an OSC int32 and preserves its exact value. A scalar fills all four
components of a `vec4`, including alpha. Wildcards use each layer's own
reflected width. Resend the scalar after changing a shader's vector width.

For a JSON-array string, supply exactly the reflected number of components.
For `ivec2/3/4`, use JSON integers in int32 range or valid expression strings.
For `vec2/3/4`, use finite numbers in float32 range or valid expression strings.
Four `vec4` components are used as raw values. A three-component RGB array
requires a prior `vec4` color binding or a prior live `ivec4` array binding
when switching to `vec4`. Alpha is set to 1. When all three components are
numeric and any exceeds 1, RGB is divided by 255.

Shared-shader commands, GLSL uniform arrays, and payloads outside this vector
path use legacy handling. Wrapped recipes use canonical `/shader/var` with
explicit flattened typed components at the reflected arity.

## `/shader/texture` and `/shader/tex`

Bind texture uniform.

Arguments:
- `target`
- `name` (uniform)
- `source`
- optional source args

`source` behavior:
- shared texture name if present in texture pool
- otherwise creates/loads texture via texture loader rules

## `/shader/buffer` and `/shader/buf`

Bind buffer texture from variable data.

Arguments:
- `target`
- `name` (uniform)
- variable source (shared var name or inline var payload)

## `/shader/reset`

Unload shader and clear attached textures/buffers/vars.

Arguments:
- `target`
