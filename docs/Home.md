# vosc

2D/3D OSC VJing software built on openFrameworks.

This documentation tracks the current typed OSC router in `main`.

## Installation

- macOS: precompiled alpha build is available at [releases/alpha](https://github.com/kasparsj/vosc/releases/tag/alpha).
- other OS: clone into your openFrameworks `apps` folder, run project generator, then build.

## Keyboard shortcuts

- `f` toggle fullscreen
- `c` toggle console window
- `TAB` toggle debug inspector
- `r` reload textures on all layers
- `u` reset all layers
- `w` toggle wireframe on all geometries
- `1..9` set blend mode on all layers (when debug view is off)
- `0` disable blend mode on all layers (when debug view is off)

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
