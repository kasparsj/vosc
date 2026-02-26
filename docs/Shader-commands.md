## Overview

Shader commands are targeted resource commands. First argument is `target`:
- `int` or numeric string: layer index
- `"*"`, `"x"`, `"a"`: all layers
- non-numeric string: shared shader name

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
- raw fragment source containing `void main(`
- local shader file/path (with optional paired `.vert`/`.geom`)
- shadertoy URL (`https://www.shadertoy.com/view/...`)

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
