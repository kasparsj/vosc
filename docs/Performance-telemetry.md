# Performance telemetry

The opt-in native control API exposes `get_performance` as a read-only API v1
query. It reports a bounded 120-originating-frame window for the composed scene:

- `frame_interval_ms` is monotonic draw-start to draw-start wall time.
- `cpu_submit_ms` is host wall time spent submitting composed-scene drawing.
- `gpu_ms` is asynchronous elapsed GPU execution for those composed-scene GL
  commands when the driver supports timer queries.

These values are not process CPU usage, system GPU usage, whole-renderer
headroom, or automatic shader protection. The measured GPU scope deliberately
excludes update-phase procedural, feedback, and media GPU submissions as well as
swap/present, debug or console overlays, and PNG preview readback. Frame interval
still observes overall draw scheduling.

The engine checks query availability before reading results, keeps eight
query objects per stream, and never waits, spins, or calls `glFinish`. A full pool increments
`dropped_samples`. Unsupported timers or zero counter bits report `supported:
false`, `state: "unsupported"`, null statistics, and a reason. Delayed results
outside the current context or the newest 120 originating frames are discarded.

## Context meaning

The timing windows reset when the native API revision, `external_epoch`, output
dimensions or feedback generation change. Feedback generation advances for
feedback source compile/reload/reset, storage, size, constant, static and step
changes. `matches_current_context` includes this generation; animation alone does
not establish stationary content. Other shader-file identity is outside this
feedback generation scope. A false value marks samples from a previous context.

## Feedback update and paired timing

`feedback` adds a separate asynchronous update stream around resource/render
update submission. It uses its own eight-query pool and closes before the
existing composed-draw interval begins. Each stream retains at most 120
originating frames. Neither pool allocates GL queries when control is disabled.
The existing `gpu_ms` remains draw-only, including for protected shader trials.

`feedback.update_gpu_ms` contains update-only statistics. `paired_gpu_ms` computes
statistics over update-plus-draw durations from the same originating frame and
configuration generation; it never sums stream percentiles. `samples` exposes
up to 120 ordered unique `{frame, update_ms, draw_ms}` pairs for independent
collection. Missing draws do not synthesize zero values. A mutation between
update and draw prevents matching; the object reports `stale` and no pairs.

`feedback_generation` identifies feedback source/configuration. `attempts`,
`draw_attempts` and `last_attempt_frame` identify originating-frame sampling
attempts in the current context. `pending_queries` and `draw_pending_queries`
report each bounded pool; `dropped_samples` and `draw_dropped_samples` count
pool/begin admission failures. Late results outside the retained window are
omitted from raw pairs; collectors use attempt counters and originating frame
IDs to account for all missing samples. Unsupported timing and stale contexts
report a reason, and empty statistics contain null values. These measurements
are not presentation latency or a sustained frame-rate guarantee.

Observation does not create a receipt, change revision, mutate the scene, or
alter queues. Monitoring and its GL resources exist only while the opt-in Unix
control server is active.

## Feedback trial measurement

`feedback_measurement` receives completions from the same query pools before
legacy epoch filtering. Its ID represents a configuration, output dimensions,
feedback generation, linked program and trial owner. Configured input samples
advance the public epoch. Their arrival leaves this measurement ID stable.
An update/draw configuration crossing is omitted and reported through
`crossing_frames` and `last_crossing_frame`.

The object exposes the recent originating attempts, submission drop counters,
per-frame intervals and raw update/draw totals. The candidate admission window
discards eight warmup origins, requires thirty pairs with at least 95% coverage,
and checks nearest-rank p95 and the age of the latest pair. The maximum includes
completed warmup and late matching pairs. See the [feedback shader trials](Feedback-shader-trials.md)
for the full policy and sustained collection rules.

## Side-by-side OSC port

`VOSC_OSC_PORT` optionally overrides the default UDP port `33333`. The value must
be strictly decimal and in `0..65535`; `0` requests an OS-assigned port. Empty,
signed, whitespace-containing, non-decimal, and out-of-range values fail before
OSC initialization. This is intended for isolated validation alongside an
existing renderer, using a separate private `VOSC_CONTROL_SOCKET`.
