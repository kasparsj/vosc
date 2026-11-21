# VOSC examples

Run a native scene with `vosc --scene examples/<name>/scene.vosc.json`.
Keep the manifest and its assets together when moving an example.

## Scene recipe and authoring examples

| Example | Native setup | SuperCollider |
| --- | --- | --- |
| Editable Scene | [JSON](editable-scene/scene.vosc.json) | [Wrapped replay](editable-scene/scene.scd) uses VisualsQuark replay API 1 and an owned renderer on port 43333. |
| Sieve | [JSON](sieve/scene.vosc.json) | [Ordinary replay](sieve/scene.scd) uses VisualsQuark and an owned renderer on port 33333. Both use the original bundled JPEG. |
| Organic Trails | [JSON](organic-trails/scene.vosc.json) | See the [protected authoring guide](organic-trails/README.md). |
| Sound Data | [JSON](sound-data/scene.vosc.json) | [Audio analysis](sound-data/README.md) uses a running SuperCollider audio server and a microphone or local audio file. |

## Built-in sources

| Example | Native setup | SuperCollider cues |
| --- | --- | --- |
| Intro | [JSON](intro/scene.vosc.json) | [Cues](intro/scene.scd) for layouts, randomized image and sketch selection, layer controls and a synthetic amplitude sample. |
| Shaders | [JSON](shaders/scene.vosc.json) | [Cues](shaders/scene.scd) for sine and input-controlled glow and color, unload and grid reload. |
| Sketches | [JSON](sketches/scene.vosc.json) | [Cues](sketches/scene.scd) for built-in sketch switching. |
| Performance | [JSON](performance/scene.vosc.json) | [Cues](performance/scene.scd) for two separate ten-second fades. |
| Basic smoke scene | [JSON](test/scene.vosc.json) | [Cues](test/scene.scd) for mixed sources, transforms, reset and unload. |

## Geometry and rendering

These scenes use the renderer's OpenGL 4.1 profile. Buffer and GPU Particles
require GPU instancing and buffer support. Grass uses a geometry stage,
Deferred Shading uses the deferred pipeline and GPU Particles uses a
floating-point render target.

| Example | Native setup | SuperCollider cues |
| --- | --- | --- |
| Camera | [JSON](camera/scene.vosc.json) | [Cues](camera/scene.scd) for model lighting, camera moves, orbit and FXAA. |
| Geometry | [JSON](geom/scene.vosc.json) | [Cues](geom/scene.scd) for switching from textured boxes to 100 transformed penguins. |
| Buffer | [JSON](buffer/scene.vosc.json) | [Cues](buffer/scene.scd) for 4,356 instanced boxes, expression-driven buffers and camera clipping. |
| Deferred Shading | [JSON](deferred/scene.vosc.json) | [Setup](deferred/scene.scd) with the original randomized red channel. |
| Clouds | [JSON](clouds/scene.vosc.json) | [Setup](clouds/scene.scd) for camera-driven ray marching and a tiled noise texture. |
| Grass | [JSON](grass/scene.vosc.json) | [Setup](grass/scene.scd) for the procedural ground grid and geometry-stage grass. |
| GPU Particles | [JSON](particles-gpu/scene.vosc.json) | [Setup](particles-gpu/scene.scd) for 16,384 particles and a shared ping-pong source. |

## Media and capture

| Example | Native setup | SuperCollider cues and prerequisites |
| --- | --- | --- |
| Video | [JSON](video/scene.vosc.json) with `--scene-input clip=/absolute/path/to/clip.mov` | [Cues](video/scene.scd) for seek, trigger and double-speed playback. Both entry points need a local MOV or MP4 with a codec the renderer can decode. Set `~clip` or `VOSC_VIDEO_CLIP` for blank-renderer SC setup. |
| Webcam Delay | [JSON](webcam-delay/scene.vosc.json) with optional `--scene-input camera=0` | [Setup](webcam-delay/scene.scd) with a separate history cue after the camera preview. Both entry points need a connected camera and Camera permission for VOSC. |
| Webcam Looper | [JSON](webcam-looper/scene.vosc.json) with optional `--scene-input camera=0` | [Cues](webcam-looper/scene.scd) to record and repeat a two-second loop, then return to the live feed. Both entry points need a connected camera and Camera permission for VOSC. |
| Ultralight | [JSON](ultralight/scene.vosc.json) with optional `--scene-input url=https://science.nasa.gov/apod/` | [Setup](ultralight/scene.scd) requires a build with `USE_OFX_ULTRALIGHT`, runtime libraries for its architecture and a reachable URL. The default build disables HTML textures. |

## Running the scene companions

Install VisualsQuark and recompile the SuperCollider class library. Open the
saved `scene.scd` and evaluate cues one block or standalone statement at a time.
Use a disposable renderer. For the V2 companions, evaluate **Setup** to
configure a blank renderer, or **Attach** after native launch to prepare the
OSC sender for live cues. Webcam Delay's separate history cue applies to blank
renderer setup. Webcam scenes use device 0 by default. Set `~cameraDevice` to
choose another camera for blank-renderer setup.

Set `~rendererPort` or `VOSC_OSC_PORT` to match your renderer. For a remote
renderer, set `~rendererHost` and `~rendererRoot` to its host and absolute path
to the copied example folder before evaluating the connection block. Use
`~pageUrl` to change the URL in Ultralight's SC setup.

After changing a manifest-backed setup, regenerate the associated commands with
`python3 scripts/generate_example_scd.py` from the repository root.

For Sieve, open the scene with `vosc --scene examples/sieve/scene.vosc.json`
or use the blank-renderer launch command in the generated script. A fresh
renderer starts in automatic onset mode. Explicit manifest gates are applied
by the script as part of setup.
