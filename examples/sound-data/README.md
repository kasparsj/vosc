# Sound Data

Use **Browse → Examples → Sound Data → Load Example**, or start VOSC with
`vosc --scene examples/sound-data/scene.vosc.json`. The scene maps layer opacity
to OSC amplitude samples received by the current renderer.

Set the host and port in the companion's `NetAddr` calls to match the same
renderer for analysis and cleanup. Use `127.0.0.1` for SuperCollider on the
render machine. The script uses port 33333 by default.

Boot the SuperCollider audio server, open [scene.scd](scene.scd), and evaluate
the first block to analyze the microphone. For a local audio file, set `file`
to its absolute path and choose an input `channel` before evaluating the block.
File mode also plays the selected channel. Evaluate the final block to stop
analysis and clear the last sample.

The script can also attach to a running renderer. Evaluate the commented
mapping lines separately to enable immediate updates and map an existing
layer's opacity.
