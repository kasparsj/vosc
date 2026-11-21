# Everyday live authoring

This workflow runs the renderer independently of the agent. Closing a chat
leaves the visual running.

## Start the renderer

From a standalone checkout on Apple Silicon, prepare the pinned openFrameworks
SDK and addons, then export both paths in the terminal that will run the launcher:

```bash
python3 scripts/setup_macos.py
export OF_ROOT="$PWD/.vosc-macos/sdks/of-0.12.1"
export VOSC_ADDON_SDK="$PWD/.vosc-macos/addon-sdks/of-0.12.1"
./scripts/run_authoring.sh
```

For openFrameworks 0.11.1, pass `--version 0.11.1` to setup and use
`of-0.11.1` in both paths. If you use `--dest`, replace `$PWD/.vosc-macos`
with that directory's absolute path. If your checkout is already under
`apps/myApps/vosc` in a configured installation, run the launcher directly.
See the [build instructions](../README.md#building-with-xcode-on-macos)
for details on using an existing installation.

The first launch builds into `~/Library/Caches/vosc-authoring/build-of-<version>`
if no binary exists. Subsequent launches reuse the build for that SDK version.

After changing C++ sources, build a separate candidate explicitly:

```bash
python3 scripts/build_macos_authoring.py \
  --output "$HOME/Library/Caches/vosc-authoring/releases/next"
```

This builds the Debug scheme for the selected SDK with a private core and
copies the runtime data into the output directory. `build-manifest.json` records
the app and core hashes and the Xcode command. Once the current renderer has been closed, launch the candidate with
`VOSC_BUILD_DIR="$HOME/Library/Caches/vosc-authoring/releases/next" ./scripts/run_authoring.sh`.

The lower-level `build_macos_legacy.py` helper remains available for explicit
prebuilt-core or sanitizer workflows. The private-core helper defaults to ASAN
for existing callers; use `--no-sanitizers` for an ordinary core. These modes are
mutually exclusive.

The default socket is `~/.local/share/vosc/control.sock`. Its parent directory
is private. The launcher refuses to start over an existing socket or a UDP 33333
listener; it does not stop another instance or delete a stale socket. Close the
renderer normally to release its socket. If a crash leaves a socket, verify no
renderer is running before removing that specific stale path.

The optional `VOSC_BUILD_DIR` and `VOSC_CONTROL_SOCKET` environment variables
change the build and socket locations. Match any socket override in MCP settings.

## Connect Codex

Install the locked adapter environment once:

```bash
uv sync --locked --project mcp
codex mcp add vosc -- "$PWD/mcp/.venv/bin/python" "$PWD/mcp/adapter.py" \
  --socket "$HOME/.local/share/vosc/control.sock"
```

This registers a user-level `vosc` MCP server. Other MCP entries are retained.
Use a new Codex session after registration so it can discover the tools.
Inspect registration
with `codex mcp get vosc`; remove it with `codex mcp remove vosc` if desired.

The MCP adapter starts with the host, connects to the supplied socket, and never
launches or resets the renderer automatically. Fourteen tools provide protected
trials, editable-scene export, performance observation,
capabilities, scene/diagnostic/receipt inspection, layer configuration, shader
publication, uniform/layer edits, and native PNG previews.

## Author a visual

A useful initial prompt:

> Use the vosc MCP tools to inspect the current scene. Create organic particles
> with flowing trails on a dark background. Capture and inspect a native preview,
> then revise compatible direct-source scenes with `trial_shader`. Poll until ready,
> inspect the preview, and explicitly accept before the lease expires. Expose a few useful float
> uniforms for speed, trail length and brightness. Save the GLSL and final uniform
> values in the project. Keep the visual running. Do not reset a nonempty scene
> without first describing what it contains.

Prefer one bounded shader first. Trail-like paths can be generated procedurally
within that shader; actual feedback textures and a persistent particle simulation
are separate creative capabilities. Use `get_scene` before edits and fresh request
IDs. Follow uncertain mutation outcomes with receipt/scene inspection rather than
blindly resending. Use preview revision/frame metadata to tie an image to an edit.

From a separate terminal, export through the renderer's control socket:

```bash
python3 scripts/scene_recipe.py --socket "$HOME/.local/share/vosc/control.sock" export NEW_DIRECTORY
```

If you launched with a socket override, use that path here.
The helper validates the descriptor and publishes `scene.vosc.json`, its
referenced assets, and replay instructions together. Keep the files together
when moving or editing the folder. Launch with
`vosc --scene NEW_DIRECTORY/scene.vosc.json`.

Add `--scd` to generate the SuperCollider companion. It uses
`OSCVisuals.replay` with VisualsQuark scene replay API 1. The
[feedback extension](Feedback-composition-contract.md) supports a bounded
shader feedback texture and requires VisualsQuark feedback replay API 1 for
SuperCollider replay. Expressions retain runtime `t`/`f`, iteration
`i`/`total`, shared controls, and input mappings.
Unsupported state such as video, custom geometry, camera, effects, buffers and
object aliases causes the whole export to fail with a reason.

## Continue and stop

Keep the renderer terminal open while working. In a new connected session,
inspect the existing scene before editing.
Only one agent should edit the scene at a time during this initial workflow.
Close the VOSC window normally when finished; MCP tools remain discoverable while
the engine is offline and report its unavailability truthfully.

## First saved example

[Organic particles and trails](../examples/organic-trails/README.md) is the first
live creative session. Its shader, final uniform recipe and native preview are
saved together. Run it in a dedicated single-layer direct-source scene to try protected edits.

## Resource acceptance before leaving a scene running

A rendered preview and successful MCP receipt do not establish efficient GPU use.
Before presenting a visual as ready for daily use:

1. Inspect shader cost as per-pixel work times resolution times frame rate. A
   bounded nested loop can still saturate the GPU. Favor local geometry, analytic
   coverage and spatial culling over checking every particle at every pixel.
2. Record resolution, particle/effect counts, controls and frame-cap/vsync settings.
   Measure system GPU and process CPU usage; do not rely on FPS alone.
3. Allow warmup, then observe load without preview captures. Inspect appearance
   and representative control extremes separately from the measurement window.
4. Require both acceptable appearance and resource headroom. Report global GPU
   contention and any workload/quality reductions explicitly. Preserve the known
   working source and controls for rollback. Do not leave a known GPU-saturating
   draft running as the completed result.

The [particle fix and reflection](../examples/organic-trails/Performance.md) records
the failure that prompted this rule, its measured correction.
For that example on the current machine, the acceptance target was roughly 60 FPS
and mean system GPU below 40% in the observed background conditions. These are
local workload criteria, not universal hardware performance promises.

## Protected edits

For a compatible single-layer direct-source baseline, use `trial_shader` with
the candidate source and its numeric overrides together. Poll `get_request` until
`ready`, capture/inspect the preview, and call `accept_trial` with the original
trial ID. Observe the terminal rendered receipt. `cancel_trial`, lease expiry and
budget rejection restore the retained program and constant bindings; observations
do not renew the lease. Unsupported baselines fail admission before replacement.

Bootstrap an empty scene with a known cheap baseline. Raw `publish_shader`,
`set_uniform` and OSC remain unbudgeted compatibility paths; do not describe their
success as performance admission. A trial covers only the composed GPU scope,
not compile stalls, GPU hangs, presentation or media/update work. Continue the
resource and visual acceptance checks above. See [trial scope](Shader-trials.md).

Existing processes keep their launched binary. Installing this code does not
upgrade a running renderer; intentionally rebuild and relaunch when appropriate.

See [editable scene export and replay](Editable-scene-contract.md) for supported scene state and replay requirements.
