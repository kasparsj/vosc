## Command Migration

This page tracks notable OSC command renames/removals across the typed router transition.

| Old Command | Current Command | Change Type | Version/Date | Notes |
|---|---|---|---|---|
| `/mic` | `/input` | renamed | main (2026-02-26) | Input setup moved to `/input <target> <deviceName> [numChannels] [bufferSize]`. |
| `/sound/data` | `/input/data` | renamed | main (2026-02-26) | Analysis payload command moved under `/input` group. |
| `/tex/fbo` | `/tex/set` | removed | main (2026-02-26) | FBO/texture settings now use `/tex/set` properties: `internalformat`, `textureTarget`, `filter`, `minFilter`, `magFilter`. |
| `/tex/tint` | `/layer/tint` | disabled | main (2026-02-26) | `/tex/tint` is disabled in runtime; use layer tint instead. |

## Notes

- See [OSC reference](OSC-reference.md) for canonical status (`supported`, `alias`, `deprecated`, `disabled`, `feature-flagged`).
- See [Architecture](Architecture.md) for lifecycle and routing ownership.
