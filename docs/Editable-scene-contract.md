# Editable scene export and replay

Export a supported scene from the local engine socket into a new folder:

```bash
python3 scripts/scene_recipe.py --socket "$VOSC_CONTROL_SOCKET" export "My scene"
```

The folder contains readable `scene.scd`, a README, and copies of the fragment
shaders and images needed for replay. The exporter validates the whole scene
before creating the folder. Choose a new destination each time. Folder export
uses Python 3.11 or later and macOS exclusive rename.

## Replay in SuperCollider

Use [VisualsQuark at the tested revision](https://github.com/kasparsj/VisualsQuark/tree/33837286c11299bc940bb8bfed286a1cd6294d16)
or a revision that exposes `OSCVisuals.sceneReplayVersion == 1`. Open and
evaluate `scene.scd` with that class library loaded. Feedback recipes also
require `OSCVisuals.sceneFeedbackReplayVersion == 1`.

The generated file contains the renderer address, dimensions, gate state, and
ordered `visuals.send(...)` calls. You can move the folder and edit its program
or assets. For a remote renderer, put the assets on the renderer host and set
the renderer-side root in `scene.scd`.

## Wrapped setup arguments

The first wrapped command after the replay envelope opener is
`/layers/reset <int32 count> <string layout>`. Count is 0–32 and layout is
`stack`, `grid`, `col`, or `row`.

Configure each slot before calibration or dependent commands:

- `/input <int32 ID or string identifier> <string device> <int32 channels> <int32 buffer>`.
  Numeric IDs are nonnegative. Use unique slots, up to 16, with no wildcard.
  Channels are 0–32 and buffer size is 64–8192. Positive channels require a
  nonempty device.
- `/input/set <same slot> <string property> <float32 value>`.
  The property is `maxAmp` or `maxLoud`. Its value is finite with magnitude
  at most 1,000,000, and `maxAmp` must be positive.

Native export records the effective explicit setup and typed position and
uniform components. Editable SuperCollider source groups vector components
into Arrays, and the wrapped packets flatten them to typed OSC arguments.
Live `/input/data` samples are supplied externally.

## Ordinary replay and truthful observation

Replay preflights the scene and its assets, clears obsolete scene state and
queued commands, then applies the setup in order. A runtime error can leave
earlier commands applied. After an `applied` result the retained
`OSCVisuals` client can send ordinary live edits.

## Supported state

- Layer count, layout, resolution, transforms, size, tint, alpha, speed,
  visibility, and supported blend and alignment settings.
- Direct-source or file-based fragment shaders using the standard vertex
  interface, and supported default or named image textures.
- Numeric bindings, expressions, shared scalar controls, and configured audio
  or OSC input mappings and calibration.
- [One bounded default feedback texture](Feedback-composition-contract.md).

Unsupported state fails the export with a reason. Examples include webcam or
video, custom geometry and shader stages, camera, lights and effects, buffers,
and shared resource aliases. The recipe captures configuration. Animation phase,
input samples, and live feedback history are recreated by running the scene.

See the [engine API](Engine-api.md#editable-scene-export) for descriptor access
and the [SuperCollider examples](../SuperCollider/README.md) for a runnable sketch.
