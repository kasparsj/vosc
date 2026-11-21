# Protected shader trials

Protected trials let a local API or MCP client temporarily install one direct-source
fragment shader, measure the composed scene with asynchronous GPU elapsed queries,
and explicitly accept it only after admission succeeds. Cancelling, expiry, a GPU
budget failure, or an eligible external-control interruption restores the retained
working GL program and its detached constant numeric bindings.

Use `trial_shader` for compatible everyday shader edits. Observe its retained
receipt with `get_request` or the bounded summary in `get_scene`. The receipt moves
through `evaluating` and `ready`; both are pending states. A ready candidate remains
provisional until `accept_trial` performs another non-blocking GPU refresh and the
accepted scene is acknowledged after draw. Use `cancel_trial` to request restoration.

The initial scope is deliberately narrow:

- exactly one existing layer, addressed as layer `0`;
- camera disabled, with no post-processing or deferred passes;
- a loaded direct-source baseline shader with no textures, buffers, file watcher,
  or Shadertoy state;
- at most 64 holder-local, non-aliased constant float bindings, each with one to
  four finite components;
- at most 32 active writable scalar/vec2/vec3/vec4 candidate uniform overrides;
- no queued legacy/API mutations or captures when the trial starts.

Admission omits the first eight originating GPU frames, then requires at least 30
post-warmup samples. Any matching candidate sample above twice the budget rejects
the trial, including a late or warmup result. Once 30 samples exist, p95 above the
budget rejects it. Acceptance additionally requires a matching-context sample no
more than eight frames old. The default budget is 8 ms and the default lease is 10
seconds; neither observations nor new socket connections renew the lease.

This protection is not a general scene transaction or GPU sandbox. It does not
cover CPU compile time, media/update-driven work, presentation, GPU hangs, arbitrary scene persistence, or background work. Existing
`publish_shader`, `set_uniform`, and OSC shader controls remain compatibility
bypasses without this GPU admission. They are still available when no trial owns
the mutation surface, and external legacy or UI control conservatively interrupts
an active trial before the new edit proceeds.

Older API v1 engines remain usable through the original compatibility operation
minimum. Calling a new trial tool against one returns the engine's structured
`unknown_method` failure; the adapter does not launch, restart, or disable the
renderer.

For a compatible default feedback texture, see [feedback shader trials](Feedback-shader-trials.md).
