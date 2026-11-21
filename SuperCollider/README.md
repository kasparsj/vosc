# SuperCollider examples

Build and start VOSC using the [project quickstart](../README.md#building-with-xcode-on-macos).
Install [VisualsQuark](https://github.com/kasparsj/VisualsQuark) in SuperCollider
and recompile the class library for examples that use `OSCVisuals`. Open each
file in SuperCollider so relative helper paths resolve. Evaluate one
parenthesized block or standalone statement at a time, in order.

The live examples use `127.0.0.1:33333`. They can replace the current layers.
Use a disposable renderer, or give it a private port with `VOSC_OSC_PORT` and
update the example's sender to match. If you reuse a renderer, disable onset
gating before starting the next example. Audio analysis can enable gating
automatically, and the example reset leaves that setting in place.

```supercollider
NetAddr("127.0.0.1", 33333).sendMsg('/onset', 0);
```

| Example | What it demonstrates |
| --- | --- |
| [Intro](intro.scd), [basic smoke scene](test.scd) | Layouts, textures, layer controls and reset. Intro also shows an OSC-fed input. |
| [Shaders](shaders.scd), [sketches](sketches.scd), [performance cues](performance.scd) | Built-in sources, expression-driven parameters and fades. Shaders also maps OSC-fed amplitude to uniforms. |
| [Camera](camera.scd), [geometry](geom.scd), [buffer](buffer.scd), [deferred shading](deferred.scd) | Camera motion, a model, geometry instancing, shader buffers and lighting. |
| [Clouds](clouds.scd), [grass](grass.scd), [GPU particles](particles-gpu.scd) | Bundled shaders with camera, geometry or feedback. [GL constants](gl.scd) is a helper these scripts load. |
| [Video](video.scd) | Playback and seeking with your own local MOV or MP4. |
| [Webcam delay](webcam-delay.scd), [webcam looper](webcam-looper.scd) | Shared webcam textures, frame history and loops. Requires an available capture device and Camera permission for the running app. |
| [External audio analysis](sound-data.scd) | Start the SC audio server first. Send analysis from a microphone or selected file channel into a VOSC expression input. |
| [Editable scene](editable-scene-example.scd) | Uses an owned renderer on port 43333 and VisualsQuark scene replay API 1. |

[Ultralight](ultralight.scd) needs an optional HTML-enabled build and a reachable
URL. The default build disables HTML textures. The local Ultralight libraries
are Intel-only, so an Apple Silicon build needs ARM64 libraries to enable
that example. [Sieve](sieve.scd) demonstrates spatial remapping with ordinary
2D textures.
