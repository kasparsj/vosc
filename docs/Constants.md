## Build/runtime constants

From `src/Config.h`:

- `GL_VERSION_MAJOR = 4`
- `GL_VERSION_MINOR = 1`
- `USE_OFX_ULTRALIGHT = 0`
- `USE_OFX_EXTREME_GPU_VIDEO = 0`
- `USE_OFX_HPVPLAYER = 0`
- `USE_OFX_IMGUI = 1`
- `INITIAL_LAYERS = 9`
- `MAX_NOTES = 10`
- `ALLOW_TEX_2D_ARRAY = 0`

## Layouts

String to enum mapping:
- `"stack" -> Layout::STACK`
- `"grid" -> Layout::GRID`
- `"col" -> Layout::COLUMN`
- `"row" -> Layout::ROW`

Enum values:
- `STACK = 1`
- `GRID = 2`
- `COLUMN = 3`
- `ROW = 4`

## Source types

`SourceMap` values:
- `"color"`
- `"video"`
- `"hpv"`
- `"gv"`
- `"shader"`
- `"shader_pp"`
- `"sketch"`
- `"image"`
- `"html"`
- `"webcam"`
- `"2d"`

## Datasources

`DataSourceMap` values:
- `"time"`
- `"const"`
- `"rand"`
- `"noise"`
- `"sin"`
- `"cos"`
- `"mic"`
- `"amp"`
- `"loud"`
- `"onset"`
- `"tidal"`

## Primitives

Supported geometry primitive names:
- `"plane"`
- `"box"`
- `"sphere"`
- `"icosphere"`
- `"cylinder"`
- `"cone"`
- `"grass"`
- `"quad"`

## Post-processing pass names

`PostPassMap` names:
- `bloom`, `convolution`, `dof`, `dofalt`, `edge`, `fxaa`, `kaleidoscope`, `noisewarp`, `pixelate`, `lut`, `contrast`, `ssao`, `htiltshift`, `vtiltshift`, `rgbshift`, `fakesss`, `zoomblur`, `bleachbypass`, `toon`, `godrays`, `rimhighlighting`, `limbdarkening`, `invert`, `glitch`, `rotate`, `pixelsort`, `beyoon`, `reflectx`, `reflecty`, `split`, `bg`, `shadowlight`, `pointlight`, `fog`

Used by:
- `/shading/passes` (global renderer passes)
- `/tex/passes` (texture-local shader passes by shared shader name)

## Behaviour enum

- `Behaviour::RANDOM_SHADER = 1`
