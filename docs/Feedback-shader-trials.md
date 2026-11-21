# Feedback shader trials

Use `trial_shader` with `target="default_feedback_texture"` and `layer=0` to
try a replacement fragment shader while the existing scene continues to render.
Check for `protected_feedback_shader_trials` in `get_capabilities`.

The supported scene is 800×600 with two layers, one default continuous
[feedback texture](Feedback-composition-contract.md) on layer 0, and camera and
effects off. Keep the reflected numeric uniforms and texture bindings intact.
Configured OSC input samples continue during evaluation. Drain queued edits,
captures and correlated replay before starting the trial.

1. Submit the replacement fragment source with a fresh request ID.
2. Poll `get_request` until the trial is `ready`, then capture and inspect a
   preview. Preview metadata identifies the trial, candidate program and frame.
3. Send `accept_trial` with the original trial ID to keep the candidate's evolved
   feedback buffers. Use `cancel_trial` to resume the retained buffers.

The renderer seeds the candidate from the current history and measures its
feedback update and composed draw. Cancellation, lease expiry and recoverable
trial failures resume the retained history with current inputs. Acceptance
releases the previous program and buffers. Export is available again once the
trial settles, and exported recipes start from white when replayed.

Competing API edits return `trial_busy`. OSC configuration changes and UI edits
interrupt a trial before the new edit takes effect. See
[performance telemetry](Performance-telemetry.md) for the measured scope and
[the MCP adapter](../mcp/README.md) for connection instructions.
