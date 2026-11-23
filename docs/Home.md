# vosc

2D/3D OSC VJing software built on openFrameworks.

This documentation tracks the current OSC and local authoring interfaces in `main`.

## Installation

- macOS: follow the [current Xcode build and quickstart](../README.md#building-with-xcode-on-macos).
- Run the [examples](../examples/README.md) or use [local MCP authoring](Live-authoring.md).

## Keyboard shortcuts

The render window opens with a welcome screen when the operator UI is enabled.
Select **Dismiss** or press **Escape** to return to the scene for this launch.
Select **Don’t show again** to skip the welcome screen on future launches.

- `f` toggle fullscreen
- `O` open the diagnostic overlay in Overview
- `C` open the overlay in Console
- `Tab` open Inspector from clean output
- `Tab` / `Shift+Tab` move focus while the overlay is visible
- `Left` / `Right` switch destinations when a dock button has focus
- `Up` / `Down` move within a focused Inspector or Console list
- `Enter` / `Space` activate the focused overlay control
- `Escape` or **Return to output** dismiss the overlay and keep the current window mode
- `r` reload textures on all layers
- `u` apply the layer reset action to each layer
- `w` toggle wireframe on all geometries
- `0`–`9` set the blend mode on all layers

An unmodified primary click on clean output opens Overview. Open a layer from Overview or the dock’s Inspector, choose
its shader, and follow **View error in Console** when a retained reload diagnostic
is available. In Inspector, choose **Layers** or **Globals**. Layer labels
match OSC identifiers. **Back** restores the originating row and its scroll
position while that resource exists. Read-only navigation leaves the scene
running. **Texture updates** can affect all bindings that share the selected
texture.

In Console, search and **Errors only** narrow the visible messages and **Copy
filtered log**. Scroll back to pause following, then use **Jump to latest** to
resume. **Clear view** resets the local log view and Overview error summary.
Runtime errors are retained without opening the overlay. The minimal Linux
profile excludes the operator UI and its shortcuts.

## Command overview

- setup/input: [Setup commands](Setup-commands.md)
- camera: [Camera commands](Camera-commands.md)
- textures: [Texture commands](Texture-commands.md)
- geometry: [Geometry commands](Geometry-commands.md)
- shaders: [Shader commands](Shader-commands.md)
- layers: [Layer commands](Layer-commands.md)
- variables: [Variable commands](Variable-commands.md)
- lights: [Light commands](Light-commands.md)
- materials: [Material commands](Material-commands.md)
- constants: [Constants](Constants.md)
- full command surface: [OSC reference](OSC-reference.md)
- migration map: [Command migration](Command-migration.md)
- runtime ownership/flow: [Architecture](Architecture.md)
- graphics backend options and migration assessment: [GPU backend research](GPU-backend-research.md)
- local authoring control: [Engine API](Engine-api.md)

## Core transport commands

These are handled by the typed parser but are not resource-domain commands:

- `/onset` enable/disable onset-gated queue processing
- `/onset/force` force queue flush on current frame
- `/dirt/play` forward a TidalCycles event payload
- `/midi` forward MIDI command group
- `/midi/list` print MIDI input ports
- `/shading` select shading mode (`deferred` enables deferred path)
- `/shading/passes` configure post/deferred passes

## Target selector model

For targeted command groups (`/tex*`, `/var*`, `/geom*`, `/shader*`, `/layer*`, `/mat*`), argument 0 is a selector:

- integer or numeric string: layer index
- `*`, `x`, `a`: all layers
- non-numeric string: shared resource name (supported for `/tex`, `/var`, `/geom`, `/shader`)

Shared-name targeting is not supported for `/layer*` and `/mat*`.
