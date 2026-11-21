# Native scene V2

VOSC loads a V2 manifest with `vosc --scene <file>`. The examples use V2
for built-in textures, shader stages, geometry, and external media.
The [scene exporter](Editable-scene-contract.md) produces V1.

## Manifest

The top level contains `version: 2`, `title`, `size`,
`commands`, and optionally `onset_gate` and `inputs`.
Begin with `/layers/reset` and the initial stack or grid layout.
All setup commands are applied in manifest order. Live performance
cues run through ordinary OSC after startup.

Each command has an `address` and an `args` array. Arguments use
`{"type": "...", "value": ...}`. The types are:

| Type | Value |
| --- | --- |
| `i`, `f`, `s` | OSC int32, float32, and string. Expression positions use strings. |
| `layer` | Zero-based layer index or `"*"` where the command allows all layers. |
| `ref` | An earlier named texture, model, or buffer in the command's resource domain. |
| `vector` | One to four typed scalar arguments. VOSC flattens shader uniform components and uses the ordinary vector representation for properties, colors, and buffer expressions. |
| `source` | An object with a `kind` and the source parameters below. |

| Command | Source kinds and parameters |
| --- | --- |
| `/tex` | `image`, `fragment`, and `feedback` with `path`. `color` with a six-digit hex `value`. `sketch` with `name`. `grid-plane` with its numeric arguments in the command. `video`, `camera`, and `html` with an `input` slot name. |
| `/geom` | `model` with a DAE `path`. Use a scalar name for the supported procedural primitives. |
| `/shader` | `stages` with a `stages` object containing `fragment` and optional `vertex` and `geometry` paths. Without a vertex stage VOSC supplies its standard passthrough shader. |

Assets belong to the example folder. Include paths and model image
references resolve from the referring file, within the same folder.
VOSC checks the asset content before renderer setup and again before
reporting readiness.

V2 accepts the startup operations and resource shapes used in the
[examples](../examples/README.md). Native limits include 32 initial
layers, 2,048 commands, 64 distinct assets, a 1 MiB manifest, and a
64 MiB decoded image budget. The example resource limits include
4,356 node entries, 16,384 particle instances and 120 history slots.
The V2 startup validator is independent of wrapped SuperCollider replay.

## Media inputs

Use `--scene-input name=value` for each override. The scene must declare the
name and its type. VOSC rejects duplicate or unknown names.

| Input kind | Value |
| --- | --- |
| `video` | Required renderer-local MOV or MP4 path. |
| `camera` | Device index, default 0 in the examples. The example textures request 640×480. |
| `html` | HTTP(S) page URL. Requires a build with `USE_OFX_ULTRALIGHT`. |

```sh
vosc --scene examples/video/scene.vosc.json --scene-input clip=/absolute/path/to/clip.mp4
vosc --scene examples/webcam-delay/scene.vosc.json --scene-input camera=0
```

VOSC prints `VOSC_SCENE_READY` once it composes the scene. Media scenes
wait for a decoded, captured, or uploaded frame. Webcam Delay also waits
for its delayed samples. A 15-second startup deadline covers media opening
through composition and exits with a nonzero status if the source stalls.
