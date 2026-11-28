# VOSC Editor

The controller imports and edits fragment shader drafts. The render host opens the draft in its own native VOSC window. The controller can stage and inspect sources without a host connection.

See the [VOSC Editor roadmap](ROADMAP.md) for the mockup audit and the proposed GLSL, Hydra, and live-preview milestones.

## Build a signed internal test package

Use a macOS arm64 machine with Xcode, Node 24, npm, Python 3.12 or newer, and an installed Developer ID Application signing identity. Provide a signed VOSC Release app staged with stock assets. The package script verifies the renderer input, prepares the arm64 slice and signs each app. The package targets macOS 15 or newer.

```sh
npm ci
node scripts/stage-triode.mjs ../../../hydra/triode
node scripts/stage-triode.mjs --verify-staged
npm run build
python3 scripts/package_macos.py \
  --output "/path/to/new-package-directory" \
  --identity "Developer ID Application: YOUR TEAM" \
  --vosc-app "/path/to/staged/vosc.app"
```

The staging command combines the shared GLSL modules from the Triode commit in
`triode-compiler-pin.json` with Editor's adapter in `compiler/vosc-single-pass.js`.
The pin records the source hashes, base revision and historical proof digest.
For an offline Editor build without a sibling Triode checkout, copy the
verified `build/triode/` directory into the Editor tree and
run `node scripts/stage-triode.mjs --verify-staged` before building. The signed
private qualification package includes the pinned compiler sources, LICENSE and
NOTICE plus the worker's compiler audit. External distribution awaits a
decision on the exact AGPL-3.0-only compiler and its use in the package.

Install `controller/VOSC Editor.app` in Applications on the controller Mac. On the render Mac, keep `VOSC Render Host.app` and `vosc.app` beside each other. Open both desktop tools from Finder.

## Pair a render host

1. On the render Mac, open VOSC Render Host and enable the draft service. Keep its host fingerprint and port visible.
2. In VOSC Editor, choose **Render Host**. Select a discovered host or enter its address and displayed port. Compare the entire displayed host fingerprint against the companion window and enter that fingerprint in the controller.
3. Enter a controller name and request pairing. Compare the entire controller fingerprint displayed on both machines. Approve the matching request in VOSC Render Host.
4. To reconnect, select the paired host from the discovered list. For a manual address, inspect it and choose **Connect paired host**. A changed host certificate needs explicit pairing again.

## Import and preview

Choose a local file, pasted GLSL, a GitHub file, a GitHub repository with a ref, or a supported Shadertoy URL. The import results retain source diagnostics, original and converted source, and known digests. Use **Validate and add selected** with a paired host connected. Open a converted source directly in the editor or open the editable copy from the library. A repeated import preserves edits in the library copy.

Authorized Shadertoy imports use the API key in the local login Keychain under service `com.vosc.authoring.shadertoy` and account `api-key`.

## Run and recover a native preview

Open or create a GLSL draft, connect to its paired render host, and choose **Run** (⌘Return). The preview window opens on the render host. The status reports a live revision after the host confirms a draw. After a compile or link failure, use **Problems** to inspect the native log. **Jump to line** selects the reported line, opening an attempted-source snapshot when the working text has changed. The previous confirmed shader and controls keep drawing while an edit is being prepared or a mapping change is under review.

Unchanged writable uniforms retain compatible fixed values and OSC mappings. When a mapping would change, review the candidate's fallback before applying it. Retained mappings can be staged against a compatible target for a later Run or discarded. Auto-run starts off. Enable it after the first successful manual Run and use **Pause auto-run** to stop scheduled attempts.

**Restore preview** uses the accepted snapshot on its original paired host. If you connect to a different host, **Use this host** selects a destination. A separate Run sends the working source to it.

## Run a single-pass JavaScript patch

Open a local JavaScript file or choose **New** and **Hydra patch**. Pair with a Render Host and
choose **Run** to compile the patch in Editor. **Generated GLSL** shows the exact
shader sent to the host. Save keeps the working patch and the last accepted
shader separately. Restore and standalone export use that accepted shader.

| Operation | Arguments and behavior |
| --- | --- |
| `osc(frequency, sync, offset)` | Defaults to `60, 0.1, 0`. Frequency must be positive. |
| `.rotate(angle, speed)`, `.rotateDeg(angle, speed)` | Angle in degrees and speed in radians per second. Defaults to `10, 0`. |
| `.rotateRad(angle, speed)` | Angle in radians and speed in radians per second. Defaults to `0.174533, 0`. |
| `.blend(source, amount)` | Defaults to `0.5`. A `param(id, default)` handle exposes the blend amount to native OSC mapping. |
| `.modulate(source, amount)` | Displaces source coordinates using another chain. Defaults to `0.1`. |
| `setFunction(definition)` | Registers a patch-local color transform with finite scalar inputs and a GLSL color body. |
| `.out()` | Ends the patch with one implicit output. |

```js
osc(11, 0.18, 0.7)
  .blend(osc(7).rotateDeg(-31), param("mix", 0.5))
  .rotate(17)
  .out();
```

JavaScript helpers and loops can build a chain during Run. Per-frame JavaScript
callbacks, feedback, multiple outputs and media belong to later milestones.

## Map OSC input

In **Inspector**, add a named slot and apply an **OSC range** to a writable shader component. Use the stable ID shown alongside the slot name and set **Low** and **High**. **Fixed** returns that component to a constant.

Choose **Enable OSC input** and use the displayed UDP port on the controller. The external sender runs on the same machine and sends one amplitude-only message to `127.0.0.1`. Use address `/input/data` with the slot ID as an OSC string and amplitude as an OSC float between 0 and 1. If the displayed port is 49152 and the slot ID is `s0123456789ab`, SuperCollider can send a midpoint sample as follows.

```supercollider
NetAddr("127.0.0.1", 49152).sendMsg("/input/data", "s0123456789ab", 0.5);
```

The relay accepts individual sample messages for configured slot IDs. The selected native draft receives the samples.

## Preview a synthetic callback uniform

Declare and use `uniform float u_vosc_callback;` in a GLSL draft, then choose **Run**. [Example fragment](tests/fixtures/callback.frag)

The native preview starts at 0.25. Editor evaluates a synthetic sine wave between 0.25 and 0.75 and sends samples at up to 10 Hz. It skips samples while a draft operation or sample is in flight. The callback owns its uniform during preview. Fixed controls and OSC can still drive other uniforms.

The host holds the last applied value when Editor disconnects or a callback stops. Controls shows the last value confirmed by a native draw and any callback error. Reconnect and choose **Restore preview** to start the callback on a new preview lease.

**Export replay** freezes the last value confirmed by Editor for the accepted artifact. A saved draft carries the snapshot taken at Save. Standalone replay uses that fixed value with the accepted fragment and existing OSC mappings.

## Save and reopen

**Save draft** writes the working source and any accepted snapshot, including its slot definitions and bindings. You can open and save drafts offline, including work with GLSL errors.

**Export replay** uses the named accepted revision and creates a new folder containing `scene.vosc.json` and `draft.frag`. Keep those files together and open the scene in a separate VOSC renderer with `vosc --scene "/path/to/scene.vosc.json"`. Runtime OSC samples stay external to the saved files.

## Run the Linux development Editor

From `desktop/authoring`, use the installed npm dependencies in a desktop
session with a keyring and a working Electron sandbox.

```sh
npm run build
./node_modules/.bin/electron build/app
```

Open **Render Host** to see renderers on this computer and the local network.
Select a host to pair. A host you've already paired connects when selected.

## Stage an internal Ubuntu Render Host

Use the x86-64 Ubuntu graphics session and a staged Linux minimal Release build
with its `bin/data` directory. Use the existing authoring build dependencies to
stage the service. On Ubuntu, use the existing Node 22, system OpenSSL,
util-linux `flock`, and Python 3.12 at `/usr/bin/python3`. The authenticated TLS
listener uses Python's standard-library `ssl.VERIFY_X509_PARTIAL_CHAIN`. It
passes authenticated requests to the Node service over an owner-only Unix
socket with a private capability token. Node manages bootstrap, pairing,
approved peers, and command dispatch. DNS-SD uses the existing system Avahi
daemon and client libraries. This internal setup requires no additional package
installation or changes to the Editor importer runtime.

From `desktop/authoring`, stage to a new directory. Supply the exact VOSC and
openFrameworks SDK Git revisions used for the renderer. If the test build has a
process-local runtime library directory, include it with `--runtime-libs`.

```sh
node scripts/stage_linux_host.mjs \
  --output "$HOME/vosc-host-test" \
  --renderer-bin "/absolute/path/to/staged/bin/vosc" \
  --source-revision "$VOSC_REVISION" --sdk-revision "$OF_REVISION"
```

In the Ubuntu graphics session, use a disposable profile with a short absolute
path and two available user ports. For testing on one machine, bind to
`127.0.0.1`. The host appears as **This computer** in a Linux Editor running
under the same user with the same private `XDG_RUNTIME_DIR`. Set `DISPLAY` and
`XAUTHORITY` to the current X11 display and the path to its session
authorization file. Run the host under a transient user service so the same
cgroup owns the host and its private children, including if the host process
exits unexpectedly. Choose an unused unit name for the test.

```sh
export VOSC_TEST_PROFILE="$HOME/.vosc-m3-test"
systemd-run --user --wait --pty --collect --service-type=exec \
  --unit=vosc-m3-test --property=KillMode=control-group --property=TimeoutStopSec=20s \
  --setenv="PATH=$PATH" --setenv="DISPLAY=$DISPLAY" --setenv="XAUTHORITY=$XAUTHORITY" \
  "$HOME/vosc-host-test/vosc-render-host" serve \
  --state-dir "$VOSC_TEST_PROFILE" --bind 127.0.0.1 \
  --bootstrap-port 47491 --rpc-port 47492
```

For discovery from another machine, use `--bind "$LAN_IPV4"` and add
`--interface "$LAN_INTERFACE"`. The Ubuntu host advertises through Avahi on
that interface. The Linux Editor build uses `/usr/bin/python3` and the system
Avahi client libraries to look for compatible hosts. LAN browsing needs a
running Avahi daemon. The same-user listing works without it.

The readiness record contains the target ID, host fingerprint and both ports.
The renderer starts on demand when an approved Editor requests a preview or
shader validation.
In the Editor, choose **Render Host**, select the host and compare the full host
fingerprint before requesting pairing.
From another Ubuntu terminal, list the pending requests. Compare the complete
Editor fingerprint before approving or denying its specific request.

```sh
VOSC_TEST_PROFILE="$HOME/.vosc-m3-test"
host="$HOME/vosc-host-test/vosc-render-host"
"$host" pair list --state-dir "$VOSC_TEST_PROFILE"
"$host" pair approve "$REQUEST_ID" "$EDITOR_FINGERPRINT" --state-dir "$VOSC_TEST_PROFILE"
"$host" pair deny "$REQUEST_ID" "$EDITOR_FINGERPRINT" --state-dir "$VOSC_TEST_PROFILE"
"$host" peer revoke "$EDITOR_FINGERPRINT" --state-dir "$VOSC_TEST_PROFILE"
```

Use one decision per request. Stop the test service with
`systemctl --user stop vosc-m3-test.service` from another terminal. Restart
using the same profile to retain the host identity and approved peers. To
exercise manual address, restart with `--no-discovery` and omit `--interface`.
Enter the bind address and bootstrap port in the Editor.
