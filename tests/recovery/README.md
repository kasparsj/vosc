# Native runtime tests

The harnesses launch the explicitly supplied renderer with an isolated Unix
socket and OSC port.

- The [feedback trial harness](../../scripts/test_feedback_trial.py) checks
  interruption on native resize and conflict receipts during cancellation.
- The [editable scene harness](../../scripts/test_scene_recipe.py) checks
  resize and explicit transforms during replay.
- The [OSC receiver shutdown procedure](../../docs/shutdown-lifecycle.md)
  covers graceful shutdown under idle and active OSC traffic.

Run the relevant harness with `--help` for its inputs and options.
The feedback and shutdown fault-injection hooks require a test build with
`VOSC_RECOVERY_TESTING`.
