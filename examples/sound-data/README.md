# Sound Data

Start VOSC with `vosc --scene examples/sound-data/scene.vosc.json`. The scene
listens for OSC analysis on port 33333 and maps layer opacity to amplitude.

Boot the SuperCollider audio server, open [scene.scd](scene.scd), and evaluate
the first block to analyze the microphone. For a local audio file, set `file`
to its absolute path and choose an input `channel` before evaluating the block.
File mode also plays the selected channel. Evaluate the final block to stop
analysis and clear the last sample.

The script can also attach to a running renderer. Evaluate the commented
mapping lines separately to enable immediate updates and map an existing
layer's opacity.
