# Local engine authoring API

The [MCP adapter](../mcp/README.md) and command-line clients use this API. The
acceptance record below includes evidence from the original milestone 2 build.

## Scope and decisions

- Opt in with `VOSC_CONTROL_SOCKET=/absolute/private-directory/control.sock`.
  Without it, normal OSC startup is unchanged. Use a caller-owned private directory.
- Use a local Unix socket, mode 0600, with newline-delimited JSON requests and
  responses. Refuse to replace existing paths. No network listener or new dependency.
- Poll nonblocking sockets on the main update thread with bounded connections,
  request sizes, response buffers, and work per frame. Graphics work stays on the
  render thread. Shader compilation and requested pixel readback can still stall
  a frame; this is not a latency or hostile-shader sandbox guarantee.
- Initial mutations: configure layer count/layout, publish fragment source to one
  layer, set an active float/vector uniform, and set layer alpha/visibility.
  General OSC forwarding, arbitrary file access, remote shaders/media, multi-command
  transactions, and arbitrary scene serialization are outside this API.
- Reuse the existing parser/handlers for validated equivalent commands; report
  success only after the supported operation's actual outcome is known.
- A mutation has a caller-supplied ID and explicit `immediate` or `onset` timing.
  The API onset queue is separate from the OSC queue. It follows the existing
  gate, including forced onset; immediate API work does not flush that queue.
- Store bounded receipts. Identical retained mutation IDs return the stored result;
  reusing a retained ID with different content fails. Evicted IDs are not permanent
  deduplication keys. Pending work is never silently evicted.
- Inspect current engine objects rather than reconstructing state from command
  history. A control revision identifies executed API mutations, not elapsed
  animation time or a scene lock against external OSC/file-watch edits. The separate
  `external_epoch` counts legacy dispatch and UI input conservatively.
- Capture the final composed output after postprocessing and before debug/console
  overlays. Return PNG data, dimensions, frame identity, and the control revision
  represented by that rendered frame. Require a requested minimum applied revision
  when callers need a preview after an edit.
- Compile explicit shader replacements into candidates; failed compilation must
  preserve the previous program and configured variables/textures. Automatic
  addon file-watch reload behavior is outside this explicit-replacement contract.

## Request surface

Every request is a JSON object with `id`, `method`, and an optional `params` object.
IDs are strings; responses retain the caller's ID. This is a versioned local API,
not an MCP or JSON-RPC implementation.

| Method | Purpose |
| --- | --- |
| `get_capabilities` | Protocol version, enabled API operations, feature flags, limits |
| `get_scene` | Actual layers, loaded shaders/uniforms, layout, pending receipts, frame/revision |
| `get_performance` | Bounded frame/CPU submission/composed-scene GPU timings; see [scope and schema](Performance-telemetry.md) |
| `get_diagnostics` | Bounded structured log entries with sequence cursors |
| `get_request` | Poll a retained mutation's status/result |
| `export_scene` | Inspect retained configuration and return a strict editable OSC recipe descriptor |
| `configure_layers` | Bounded layer count and known layout |
| `publish_shader` | Compile and replace one layer's fragment shader |
| `trial_shader` | Temporarily install and measure a compatible direct-source or default feedback texture candidate |
| `accept_trial` | Explicitly accept a ready candidate after fresh GPU admission |
| `cancel_trial` | Restore the working program and bindings or the previous feedback program and history |
| `set_uniform` | Validate an active supported uniform and configure its value |
| `set_layer` | Set alpha or visibility on an existing layer |
| `capture_preview` | Request a bounded PNG from the next eligible rendered frame |

The binding limits, receipt behavior, examples, and verification scope follow below.

## Binding protocol rules and limits

- `id`: nonempty string, at most 64 bytes. Unknown methods, extra parameters,
  wrong types, nonfinite/out-of-range values and malformed JSON fail before mutation.
- Responses include `id`, `ok`, `status`, control `revision`, and last rendered
  `frame`. Failures have `error.code` and `error.message`; shader failures also have
  request-correlated `error.diagnostics`. Queries return data in `result`.
- Mutation states: `queued` → `applied` → `rendered`, or `queued` → `failed`.
  Immediate work may return `applied` directly. Only successful execution increments
  the applied revision; failure does not. The rendered revision/frame is advanced
  only after composition. Queued targets/uniforms are revalidated at execution;
  failure does not stop later requests. Pending work times out after 30 seconds.
- `configure_layers`: integer count 0–32 and layout `stack`, `grid`, `row`, or `col`;
  reinitializes layers using the existing engine behavior.
- `publish_shader`: existing integer layer index and nonempty fragment source up to
  128 KiB. Explicit API and ordinary OSC replacement paths preserve the previous
  program, variable bindings, and textures after compile or link failure.
- `set_uniform`: existing layer, active non-array float/vec2/vec3/vec4 uniform name,
  matching scalar/array of finite numbers with absolute value at most 1,000,000.
  Engine-managed time/resolution/random/light/offset/layer uniforms, OF globals,
  layer-variable names, and bound texture/buffer size uniforms are read-only.
- `set_layer`: existing layer, property `alpha` (finite number 0–1) or `visible`
  (boolean). Mutations accept timing `immediate` (default) or `onset`.
- `capture_preview`: maximum width 64–1024 (default 640), optional readback budget
  `max_readback_pixels` 1–16,777,216 (default 16,777,216), minimum revision between
  zero and the current applied revision. A future/queued-only revision is rejected.
  Returns a queued receipt; poll `get_request` for `captured` and PNG base64 data.
  Frames exceeding the caller's readback budget fail before allocating pixels;
  readback failures return `failed`. Only two completed image payloads are retained;
  older capture receipts explicitly become `expired` rather than claiming an image.
- Bounds: 8 clients; 256 KiB input/line per client; JSON nesting ≤64; 8 MiB output
  per client; one request/read/write chunk per client per update (64 KiB I/O chunks);
  16 pending mutations/captures; 64 retained receipts; one capture per rendered frame;
  16 megapixels maximum readback. Reject queue overflow before execution, close
  oversized/undrainable clients, and expire idle connections after 10 seconds.
  Pending receipts cannot be evicted by receipt pressure; completed receipts may be.
- Diagnostics: monotonically increasing sequence cursors, last 512 records retained,
  at most 128 returned, bounded message lengths; report an explicit retention gap.
- Parent socket directory must be private (owned by the current user, no group/other
  access). Shutdown removes only the exact socket inode created by this instance;
  existing and replacement paths must survive.

## Scope inventory and acceptance record

Needs for every live target: current-source compatibility build, opt-in engine in
an isolated private directory, available local socket client and GPU context. The
normal-start target additionally runs without the opt-in environment variable.
CLI/unit targets need only the compiler/Python harness. Each target below has its
own verdict; shared implementation does not combine acceptance claims.

| Target | Surface | Behavior/scenarios | Required evidence |
| --- | --- | --- | --- |
| VAL-API-START | CLI + API | Disabled by default; opt-in private socket mode 0600; existing paths and replacement paths preserved; graceful cleanup | Live normal/opt-in startup plus socket ownership/lifecycle regression tests |
| VAL-API-TRANSPORT | API | Partial and coalesced lines, malformed/deep/oversize JSON, client cap, half-close, disconnect, slow output, bounded per-update service | Real-socket regression tests and live continued frame advancement under traffic |
| VAL-API-LAYERS | API | Count/layout valid cases and empty scene; negative/too-large/fractional count and unknown layout rejected without state mutation | Live requests and actual layer inspection |
| VAL-API-SHADER | API + GPU | Valid source applies; invalid layer/empty/oversize source rejected; valid-invalid-valid replacement works | Live receipts, actual active program IDs, uniform/texture bindings and decoded previews |
| VAL-API-UNIFORM | API + GPU | Scalar and vec2/3/4 values work; unknown/reserved/array/type/size/nonfinite/range errors preserve state | Live requests, configured and GPU-rendered values after draw |
| VAL-API-LAYER | API | Alpha/visibility work; invalid property/type/range/target rejected without partial change | Live request/state checks |
| VAL-API-RETRY | API | Identical pending/final mutation ID returns stored receipt; conflicting content fails; completed eviction explicit; pending work retained | Live duplicate/conflict and retention/pressure scenarios |
| VAL-API-QUEUE | API + OSC | Open/closed/forced gates; immediate API changes do not release waiting OSC commands; stale queued target/uniform fails and later work continues; timeout/overflow fail truthfully | Live mixed OSC/API trace, scene state, receipt transitions and revisions |
| VAL-API-COMPILE | API + GPU | Compile failure gives correlated driver diagnostics and preserves program, variables and textures | Live compile failure after working textured/uniform scene and continued rendered output |
| VAL-API-LINK | API + GPU | Link failure gives correlated driver diagnostics and preserves program, variables and textures | Separate live link failure and unchanged actual program/resources |
| VAL-API-SCENE | API | Queries actual empty/unloaded/loaded layers and supported authoring state; ordinary OSC edits appear; configured values distinguished from last-rendered GPU values | Live OSC mutation followed by scene query |
| VAL-API-CAPABILITIES | API | Reports version, enabled operations/features, numeric limits and revision scope | Real capability response compared with exercised behavior |
| VAL-API-DIAGNOSTICS | API | Empty/new/cursor/retention-gap cases; bounded entries; shader error correlation | Live cursor/overflow and shader failure responses |
| VAL-API-PREVIEW | Artifact + API | Decodable bounded PNG after requested applied revision, accurate frame/revision/dimensions, overlays excluded; future revision/readback failure cannot be reported captured | Actual PNG inspection with overlays enabled, receipt/frame checks, rejected future revision and expired image payload |
| VAL-API-OSC | OSC + GPU | Existing OSC shader/uniform workflow remains operational; invalid explicit OSC replacement also preserves working shader | Existing harness plus live OSC valid/invalid replacement regression |

Implementation ownership: one shader loader slice owns explicit replacement
preservation; one local-control slice owns transport, receipts, queries and capture
integration. Parent owns integration and fresh live validation across all targets.
No OMX mission runtime is exposed in this session, so execution uses native bounded
agents and this repository acceptance record.

## Usage

Build the current Debug renderer into a private directory:

```bash
BUILD_DIR="$(mktemp -d /tmp/vosc-api-build.XXXXXX)"
python3 scripts/build_macos_authoring.py --output "$BUILD_DIR"
```

Create a private control directory and start a disposable renderer (ensure port
33333 is free). Keep this environment value for client commands in another shell:

```bash
CONTROL_DIR=$(mktemp -d /tmp/vosc-control.XXXXXX)
export VOSC_CONTROL_SOCKET="$CONTROL_DIR/control.sock"
cd "$BUILD_DIR/products"
./voscDebug.app/Contents/MacOS/voscDebug
```

From the repository root, with the same `VOSC_CONTROL_SOCKET` value:

```bash
python3 scripts/control_client.py --socket "$VOSC_CONTROL_SOCKET" get_capabilities
python3 scripts/control_client.py --socket "$VOSC_CONTROL_SOCKET" configure_layers '{"count":1,"layout":"stack"}' --wait
python3 scripts/control_client.py --socket "$VOSC_CONTROL_SOCKET" get_scene
```

Run this Python example from the repository root after configuring one layer:

```python
import base64
import os
from pathlib import Path

from scripts.control_client import Client

socket_path = Path(os.environ["VOSC_CONTROL_SOCKET"])
fragment_source = """#version 330
out vec4 fragColor;
void main() {
    fragColor = vec4(0.2, 0.4, 0.6, 1.0);
}
"""

client = Client(socket_path)
try:
    published = client.call("publish_shader", {"layer": 0, "source": fragment_source})
    assert published["ok"], published
    rendered = client.wait(published["id"])
    assert rendered["ok"], rendered
    requested = client.call("capture_preview", {"minimum_revision": rendered["revision"]})
    assert requested["ok"], requested
    captured = client.wait(requested["id"])
    assert captured["ok"], captured
    preview = socket_path.with_name("preview.png")
    preview.write_bytes(base64.b64decode(captured["result"]["data_base64"]))
finally:
    client.close()
```

A queued reply acknowledges acceptance, not execution. `get_request` takes
`{"request_id":"original-id"}` and returns that receipt in `result`. An applied
receipt contains `applied_after_frame`; rendered receipts contain the subsequent
rendered `frame`. Captures return `status: captured`, image metadata/base64 in
`result`, and the actual `frame`, API `revision`, and `external_epoch` at capture.
Expired image payloads have `status: expired`, `ok: false`, and code
`preview_expired`. Unknown/evicted receipts return `request_not_found`. Receipt
state and retry protection are process-local and do not survive restart.

Uniform inspection exposes GL type, array/writable flags, last-drawn GPU values
as `rendered_value` strings, and configured float bindings as arrays. Expressions
configured by OSC are labelled `expression`; their source expression is not
serialized by `get_scene`. Use `export_scene` for supported source expressions and
the assets needed for replay. Use `get_scene` after ordinary OSC changes to
observe current objects; API revisions do not version every external OSC,
hot-reload, or animation change. `external_epoch` provides a conservative legacy
dispatch/UI counter.

Explicit source/local-file shader replacement now validates each required stage
and the final GL link status. Candidate failure keeps the active program, variable
bindings and textures. Automatic addon watcher behavior and remote Shadertoy cache
file behavior have not been exercised by these local acceptance checks.

## Validation

```bash
./scripts/run_control_tests.sh
SANITIZER=address ./scripts/run_control_tests.sh
SANITIZER=undefined ./scripts/run_control_tests.sh
./scripts/run_osc_tests.sh
python3 -m unittest discover -s tests -p 'test_legacy_build.py'
python3 scripts/generate_osc_reference.py --check
bash scripts/check_md_links.sh
```

The transport suite uses real sockets and exercises framing, limits, half-close,
slow/disconnected clients, private paths, inode-safe cleanup and idle timeouts.
The GPU acceptance script requires a disposable running instance and **resets
layers, shader state, and onset gating**:

```bash
python3 scripts/test_engine_api.py --socket "$VOSC_CONTROL_SOCKET" --output /tmp/vosc-api-evidence --allow-reset
```

It checks actual GPU uniform values and program/resource preservation, mixes API
and OSC queues, waits for the real 30-second timeout, tests diagnostic retention,
and decodes returned PNGs. A static-color image oracle checks every output pixel
against RGB (51, 102, 153), catching debug/console contamination. A caller-imposed
one-pixel readback budget exercises a real asynchronous capture rejection; GPU
hardware/device-loss faults are not manufactured.

See the [MCP adapter](../mcp/README.md) for the stdio interface.

## Additive performance telemetry

The read-only `get_performance` query takes no parameters and uses the existing
version1 envelope without creating a mutation receipt or incrementing revision.
See [performance telemetry](Performance-telemetry.md) for measured scopes, window
freshness, unsupported GPU state and the optional `VOSC_OSC_PORT` startup override.

## Additive protected shader trials

API v1 additionally advertises `trial_shader`, `accept_trial`, and `cancel_trial`.
The original operation minimum remains compatible; old engines may return
`unknown_method` for these additions. See [protected trials](Shader-trials.md)
for eligibility, receipts, and budget scope. See [feedback shader trials](Feedback-shader-trials.md)
for the separately advertised feedback target.

`trial_shader` takes `layer: 0`, fragment `source`, optional numeric `uniforms`,
`gpu_budget_ms` (default 8, range 0.05–16), and `ttl_ms` (default 10000, range
1000–30000). Accept and cancel take the original `trial_id`; all three use fresh
mutation request IDs. Poll the original receipt with `get_request`; `ready` is
provisional, not a rendered acceptance. Acceptance and restoration are acknowledged
only after their matching draw. Competing API mutations return `trial_busy`;
legacy/UI edits interrupt the trial before applying the newer edit.

## Editable scene export

Call `export_scene` with empty params. The read-only operation returns a
versioned descriptor for supported scenes and rejects unsupported retained
state. Use [the scene folder exporter](Editable-scene-contract.md) to write an
editable SuperCollider program with its assets. For the bounded feedback
extension, see [its contract](Feedback-composition-contract.md).
