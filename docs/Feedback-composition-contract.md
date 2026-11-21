# Editable feedback scenes

[Editable scene export](Editable-scene-contract.md) supports one holder-local
default shader feedback texture. The folder copies the retained fragment shader
and contains an editable `/tex` call with a `shader_pp:` asset. Replay requires
VisualsQuark feedback replay API 1 and initializes both buffers to white before
the first update.

## Supported feedback configuration

- One 2D RGBA8 texture pair, up to 800×600, with linear filtering and
  clamp-to-edge. Each buffer has one color attachment.
- Standard ping-pong fragment shader with default holder settings. The recipe
  preserves whether the texture is static or continuous.
- Up to 64 holder-local constant float controls with matching scalar or vec2,
  vec3 or vec4 uniforms. Runtime expressions and configured inputs remain
  available on other supported scene objects.
- A fresh pair of buffers on replay. Measured GPU time and live pixel history
  are outside the recipe.

A static texture runs its first update once. A continuous texture keeps
updating. The renderer reports `applied` after the qualifying feedback update
and composed draw. A shader compile or allocation failure can leave the earlier
OSC setup applied. Use [performance telemetry](Performance-telemetry.md) to
observe feedback updates, or [feedback shader trials](Feedback-shader-trials.md)
to evaluate a replacement in the supported live scene.
