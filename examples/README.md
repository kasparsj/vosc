# VOSC examples

Run a native scene with `vosc --scene examples/<name>/scene.vosc.json`.
Keep the manifest and its assets together when moving an example.

| Example | Native setup | SuperCollider |
| --- | --- | --- |
| Editable Scene | [JSON](editable-scene/scene.vosc.json) | [Wrapped replay](editable-scene/scene.scd) uses VisualsQuark replay API 1 and an owned renderer on port 43333. |
| Sieve | [JSON](sieve/scene.vosc.json) | [Ordinary replay](sieve/scene.scd) uses VisualsQuark and an owned renderer on port 33333. Both use the original bundled JPEG. |
| Organic Trails | [JSON](organic-trails/scene.vosc.json) | See the [protected authoring guide](organic-trails/README.md). |
| Sound Data | [JSON](sound-data/scene.vosc.json) | [Audio analysis](sound-data/README.md) uses a running SuperCollider audio server and a microphone or local audio file. |

The [remaining SuperCollider sketches](../SuperCollider/README.md) describe their
own prerequisites and evaluation order.

After changing the Editable Scene or Sieve manifest, regenerate its
SuperCollider setup with `python3 scripts/generate_example_scd.py` from the
repository root.

For Sieve, open the scene with `vosc --scene examples/sieve/scene.vosc.json`
or use the blank-renderer launch command in the generated script. A fresh
renderer starts in automatic onset mode. Explicit manifest gates are applied
by the script as part of setup.
