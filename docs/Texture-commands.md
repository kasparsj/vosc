## Overview

Texture commands are targeted resource commands. The first argument is always `target`:
- `int` or numeric string: layer index
- `"*"`, `"x"`, `"a"`: all layers
- non-numeric string: shared texture name

Supported commands:
- `/tex`
- `/tex/data`
- `/tex/choose`
- `/tex/reload`
- `/tex/unload`
- `/tex/clear`
- `/tex/size`
- `/tex/set`
- `/tex/color`
- `/tex/tint` (disabled)
- `/tex/speed`
- `/tex/seek`
- `/tex/var` and `/tex/uniform`
- `/tex/var/lifo`
- `/tex/passes`
- `/tex/shader/*`
- `/tex/looper`

## `/tex`

Load or replace texture source.

Arguments:
- `target`
- `source` (string)
- optional numeric args passed to source loader

`source` resolution:
- explicit type: `type:value` (for example `webcam:0`)
- file extension: `.frag`, `.jpg/.jpeg/.png`, `.mov/.mp4`, `.hpv`, `.html`
- URL loads HTML texture
- hex color string (for example `0xff0000`)
- sketch name

Examples:
```supercollider
~visuals.sendMsg('/tex', 0, '0xff0000');
~visuals.sendMsg('/tex', 0, 'images/pic.jpg');
~visuals.sendMsg('/tex', 0, 'shaders/Water.frag');
~visuals.sendMsg('/tex', 0, 'webcam:0');
~visuals.sendMsg('/tex', 'sharedCam', 'webcam:0');
~visuals.sendMsg('/tex', 1, 'sharedCam');
```

## `/tex/data`

Upload raw color data into texture.

Arguments:
- `target`
- pixel/color payload (same typed var parsing rules as `/var`)

## `/tex/choose`

Create random texture source type.

Arguments:
- `target`
- `type` (optional string from [Source types](Constants.md#source-types)); empty means random type
- optional numeric args for selected texture type

## `/tex/reload`, `/tex/unload`, `/tex/clear`

Lifecycle commands.

Arguments:
- `target`

## `/tex/size`

Set texture render size.

Arguments:
- `target`
- `w` or `w,h` or `w,h,d`

## `/tex/set`

Set texture/render properties.

Arguments:
- `target`
- `property` (string)
- property value args

Supported properties:
- `textureWrap` / `wrapMode`: one or two GL wrap values
- `numFrames`: int frame history length
- `static`: bool
- `needsUpdate`: bool
- `noClear`: bool
- `blendMode`: int (`ofBlendMode`)
- `aspectRatio`: bool
- `internalformat`: GL internal format
- `textureTarget`: GL texture target
- `filter`: set min+mag filter
- `minFilter`
- `magFilter` (`maxFilter` alias)

Note: old `/tex/fbo` command was removed; these settings moved to `/tex/set`.

## `/tex/color`

Set texture color var.

Arguments:
- `target`
- color payload (constant, expression, datasource)

## `/tex/tint`

Currently disabled in runtime.

Behavior:
- logs error: `"/tex/tint is disabled. Use /layer/tint."`

## `/tex/speed`

Set playback/update speed var.

Arguments:
- `target`
- `speed`

## `/tex/seek`

Set `timePct` var.

Arguments:
- `target`
- time percentage payload

## `/tex/var` and `/tex/uniform`

Set texture-scoped variable.

Arguments:
- `target`
- `name`
- value payload

## `/tex/var/lifo`

Append to float history and keep only last `N` values.

Arguments:
- `target`
- `name`
- `value` (float)
- `maxLen` (int)

## `/tex/passes`

Attach shader passes to texture output.

Arguments:
- `target`
- `shaderName1`, `shaderName2`, ... (shared shader keys)

## `/tex/shader/*`

Forward to shader-backed textures only (`ShaderTex`).

`/tex/shader/...` is transformed to `/shader/...` internally.

Examples:
```supercollider
~visuals.sendMsg('/tex/shader/var', 0, 'u_amount', 0.5);
~visuals.sendMsg('/tex/shader/texture', 0, 'u_tex1', 'sharedCam');
```

## `/tex/looper`

Enable/disable texture looper.

Arguments:
- `target`
- `durationSec` (float, `0` disables)
- `fps` (int, optional, default `30`)
- `speed` (float, optional, default `2`)
