## Overview

VOSC is split into runtime subsystems with `VOSC` acting as orchestrator.

Main objects:
- `VOSC`: app lifecycle and subsystem wiring
- `RuntimeContext`: camera, input state singleton reference, tidal parser
- `InputSystem`: OSC/MIDI ingress only
- `CommandSystem`: typed parse + dispatch + onset queueing
- `RenderSystem`: layer graph, shading pipeline, drawing
- `ResourceSystem`: scoped `ResourceRegistry` lifecycle + pool updates

## Update flow

Per frame (`VOSC::update`):
1. `camera.preUpdate()`
2. `CommandSystem::update()`
3. `Inputs::update()`
4. note trimming (`MAX_NOTES`)
5. `ResourceSystem::update(notes)`
6. `RenderSystem::update(notes)`
7. `camera.update()`

## Command flow

1. `InputSystem` pulls incoming OSC.
2. `CommandSystem` parses each message with `osc::CommandParser`.
3. Non-queued commands execute immediately via `CommandRouter` handlers.
4. Queued commands (`SHADING_MODE`, `SHADING_PASSES`, `TARGETED_RESOURCE`) are buffered.
5. Queue flush happens when onset gate passes (`/onset`, `/onset/force`, tidal notes, or input onset).

## Resource ownership

`ResourceSystem` owns a `ResourceRegistry` instance:
- activates it on setup (`resources.activate()`)
- deactivates/clears on shutdown
- provides scoped backing storage for pools (`TexturePool`, `GeomPool`, `ShaderPool`, `VariablePool`)

This removes static teardown ordering issues by tying pool lifetime to app lifecycle.

## Layer/resource targeting model

Targeted command families (`/tex`, `/var`, `/geom`, `/shader`, `/layer`, `/mat`) use selector arg `0`:
- index -> route to one layer
- wildcard -> route to all layers
- shared-name -> route to shared pools (supported for tex/var/geom/shader)

## Rendering responsibilities

`RenderSystem` owns:
- layer list setup/reset/layout
- post/deferred pass configuration (`/shading`, `/shading/passes`)
- draw lifecycle (`beginDraw`/`doDraw`/`endDraw`)
- keyboard shortcuts affecting render/layers

## Shutdown order

`VOSC::exit`:
1. `renderSystem.shutdown()`
2. `resourceSystem.shutdown()`
3. `runtime.tidal.reset()`

`ResourceRegistry::clear()` enforces destruction order:
- shader-owned state
- texture/geom pools
- variable pools
