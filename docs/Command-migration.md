## Command Migration

This page tracks notable OSC command renames/removals across the typed router transition.

| Old Command | Current Command | Change Type | Version/Date | Notes |
|---|---|---|---|---|
| `/mic` | `/input` | renamed | main (2026-02-26) | Use [live input setup](Setup-commands.md#input), with optional device, channel count and buffer size for a single slot. |
| `/sound/data` | `/input/data` | renamed | main (2026-02-26) | `/sound/data` is rejected. Configure the slot with [`/input`](Setup-commands.md#input), then send [`/input/data`](Setup-commands.md#inputdata) samples. |
| `/tex/fbo` | `/tex/set` | removed | main (2026-02-26) | FBO/texture settings now use `/tex/set` properties: `internalformat`, `textureTarget`, `filter`, `minFilter`, `magFilter`. |
| `/tex/tint` | `/layer/tint` | disabled | main (2026-02-26) | `/tex/tint` is disabled in runtime; use layer tint instead. |

## Notes

- See [OSC reference](OSC-reference.md) for canonical status (`supported`, `alias`, `deprecated`, `disabled`, `feature-flagged`).
- See [Architecture](Architecture.md) for lifecycle and routing ownership.
