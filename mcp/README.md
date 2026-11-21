# vosc MCP adapter

The production adapter exposes fourteen typed stdio MCP tools over the opt-in local
vosc engine API. It never discovers, launches, restarts, resets, or stops a
renderer on its own. The operator supplies an absolute Unix socket inside an
owned directory with no group/other permissions; the socket must be owned by the
same user with mode `0600`.

Install and launch from the repository root:

```bash
uv sync --locked --project mcp
uv run --locked --project mcp python mcp/adapter.py \
  --socket /absolute/private/control.sock
```

The renderer may be offline at adapter startup, so tool discovery remains
available. Calls then return a structured `adapter_engine_unavailable` error.
`mcp/server.py` remains a separate synthetic SDK compatibility probe and is not
the production entry point.

For a repeatable renderer launch, persistent Codex registration and a creative
session, see [Everyday live authoring](../docs/Live-authoring.md).

## Tools and lifecycle

Read tools are `get_capabilities`, `get_scene`, `get_diagnostics`,
`get_request`, `get_performance`, and `export_scene`. Mutation
tools are `configure_layers`, `publish_shader`, `set_uniform`, `set_layer`,
`trial_shader`, `accept_trial`, and `cancel_trial`; `capture_preview` consumes
bounded engine receipt/readback resources and is also non-read-only.
`configure_layers` explicitly resets existing layers and is
marked destructive. All tools are closed-world and advertise the annotation
matrix from the reviewed contract.

Mutation and capture IDs are required, ASCII, bounded to 64 characters, and may
not use the reserved `_q.` prefix. The adapter does not retry mutations. A lost
reply after bytes may have been written returns `unknown`/`uncertain`; inspect
the receipt and scene before choosing a new action. Receipt deduplication is
bounded and process-local, not an exactly-once guarantee.

A renderer advertising `protected_feedback_shader_trials` accepts
`trial_shader(target="default_feedback_texture", layer=0, source=...)` in the
bounded 800×600 two-layer feedback scene. Keep the existing texture bindings
and use the default ten-second lease. Configured OSC samples continue to update
the scene while it evaluates. A preview queued during the trial carries the
trial ID, evaluation revision, program and composed frame. Accept after
`ready` to keep the candidate and its evolved history. The accepted draw
releases the previous program and buffers. Cancelling or letting the lease expire
resumes the previous feedback pair. Export becomes available when the trial
settles. See the [feedback shader trials](../docs/Feedback-shader-trials.md).

Successful captures contain one native `image/png` content block plus strict
structured metadata. The base64 payload is removed from structured content and
is never copied into text. Closing the MCP host closes only adapter-owned
connections; the renderer remains owned by the operator.

Layer alpha/tint follows the existing renderer: a custom fragment shader must use
`uniform vec4 globalColor` and apply it to output if it should respect those
controls. Final opacity can be nonlinear through the legacy compositing path;
`visible=false` hides the layer independently. See the acceptance record for the
observed MCP/OSC parity and limitation.

## Verification

CPU-only locked verification:

```bash
uv run --locked --project mcp python -m unittest discover -s mcp/tests -v
```

The suite uses the official MCP client and real Unix socket peers. It verifies
discovery, closed schemas, annotations, native image projection, endpoint
rejection, capability preflight, ambiguous delivery, and subprocess cleanup.
It does not claim GPU or image-capable-agent acceptance.

The live harnesses attach only to a caller-owned disposable renderer and require
an explicit destructive opt-in:

```bash
uv run --locked --project mcp python scripts/test_mcp_api.py \
  --socket "$VOSC_CONTROL_SOCKET" --output /absolute/evidence/engine --allow-reset
uv run --locked --project mcp python scripts/test_mcp_agent.py \
  --socket "$VOSC_CONTROL_SOCKET" --output /absolute/evidence/agent --allow-reset
```

Neither harness launches or stops the renderer. The second invocation creates
ephemeral, session-only Codex MCP configuration and records the exact command,
events, final response, stderr, and exit code.

Each deterministic API-harness invocation generates and records a fresh request
ID namespace. Re-running it against the same renderer cannot reuse retained
mutation or capture receipts from an earlier evidence run.

## Performance observation

`get_performance` exposes bounded frame-cadence, CPU submission wall-time and
asynchronous composed-scene GPU elapsed-time summaries when the native driver
supports them. Unsupported GPU data is explicit; it is not reported as zero cost.
These are not system CPU/GPU utilization readings. Update-phase texture work,
presentation, debug overlays and PNG readback are outside the composed GPU scope.
Matching context identifies tracked revision/epoch/size, not shader identity or
stationary animation/file-watch content.

The original nine operations remain the compatibility minimum for old engines.
Calling telemetry on an old engine returns a structured unknown-method failure;
other tools remain usable. See [Performance telemetry](../docs/Performance-telemetry.md)
for the schema and scope. Telemetry observes cost. The separate [protected trial workflow](../docs/Shader-trials.md)
uses these measurements to admit compatible direct-source edits and restore the
working shader on cancellation, expiry or budget failure. Raw publication, uniform
edits and OSC remain unbudgeted compatibility paths. Poll a trial until `ready`,
inspect its preview, then explicitly accept it; ready does not commit the trial.

## Editable scene folders

The adapter exposes a strict version-1 editable OSC recipe descriptor. Use the
local helper to validate it and atomically publish a new scene folder:

```bash
python3 scripts/scene_recipe.py --socket "$VOSC_CONTROL_SOCKET" export "My scene"
```

Launch the exported JSON with `vosc --scene "My scene/scene.vosc.json"`.
Keep its referenced assets alongside the manifest when moving the folder.
Add `--scd` to also generate an editable SuperCollider companion. Evaluate it
with VisualsQuark scene replay API 1 (`OSCVisuals.replay`). Feedback scenes
also require VisualsQuark feedback replay API 1 for SuperCollider replay.
See the [editable scene contract](../docs/Editable-scene-contract.md).
