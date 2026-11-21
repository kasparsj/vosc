# vosc

2D/3D OSC VJing software built on openFrameworks.

This documentation tracks the current OSC and local authoring interfaces in `main`.

## Installation

- macOS: follow the [current Xcode build and quickstart](../README.md#building-with-xcode-on-macos).
- Run the [examples](../examples/README.md) or use [local MCP authoring](Live-authoring.md).

## Keyboard shortcuts

- `f` toggle fullscreen
- `c` toggle console window
- `TAB` toggle debug inspector
- `r` reload textures on all layers
- `u` apply the layer reset action to each layer
- `w` toggle wireframe on all geometries

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
