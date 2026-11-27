# VOSC Editor

The controller imports and edits fragment shader drafts. The render host opens the draft in its own native VOSC window. The controller can stage and inspect sources without a host connection.

See the [VOSC Editor roadmap](ROADMAP.md) for the mockup audit and the proposed GLSL, Hydra, and live-preview milestones.

## Build a signed internal test package

Use a macOS arm64 machine with Xcode, Node 24, npm, Python 3.12 or newer, and an installed Developer ID Application signing identity. Provide a signed VOSC Release app staged with stock assets. The package script verifies the renderer input, prepares the arm64 slice and signs each app. The package targets macOS 15 or newer.

```sh
npm ci
npm run build
python3 scripts/package_macos.py \
  --output "/path/to/new-package-directory" \
  --identity "Developer ID Application: YOUR TEAM" \
  --vosc-app "/path/to/staged/vosc.app"
```

Install `controller/VOSC Editor.app` in Applications on the controller Mac. On the render Mac, keep `VOSC Render Host.app` and `vosc.app` beside each other. Open both desktop tools from Finder.

## Pair a render host

1. On the render Mac, open VOSC Render Host and enable the draft service. Keep its host fingerprint and port visible.
2. In VOSC Editor, choose **Render Host**. Inspect a discovered host or enter its address and displayed port. Compare the entire displayed host fingerprint against the companion window and enter that fingerprint in the controller.
3. Enter a controller name and request pairing. Compare the entire controller fingerprint displayed on both machines. Approve the matching request in VOSC Render Host.
4. To reconnect an approved identity, inspect the current endpoint and choose **Connect paired host**. A changed host certificate needs explicit pairing again.

## Import and preview

Choose a local file, pasted GLSL, a GitHub file, a GitHub repository with a ref, or a supported Shadertoy URL. The import results retain source diagnostics, original and converted source, and known digests. Select **Validate imports** with a paired host connected, then choose a validated entry and **Open native preview**. A repeated import preserves edits in the library copy.

Authorized Shadertoy imports use the API key in the local login Keychain under service `com.vosc.authoring.shadertoy` and account `api-key`.

## Map OSC input

On **Draft**, add a named slot and apply an **OSC range** to a writable shader component. Use the stable ID shown alongside the slot name and set **Low** and **High**. **Fixed** returns that component to a constant.

Choose **Enable OSC input** and use the displayed UDP port on the controller. The external sender runs on the same machine and sends one amplitude-only message to `127.0.0.1`. Use address `/input/data` with the slot ID as an OSC string and amplitude as an OSC float between 0 and 1. If the displayed port is 49152 and the slot ID is `s0123456789ab`, SuperCollider can send a midpoint sample as follows.

```supercollider
NetAddr("127.0.0.1", 49152).sendMsg("/input/data", "s0123456789ab", 0.5);
```

The relay accepts individual sample messages for configured slot IDs. The selected native draft receives the samples.

## Save and reopen

**Save draft** captures the accepted shader, paired destination, slot definitions and bindings after checking the native preview receipt. **Open draft** reads the saved document offline. Connect to its paired host and choose **Restore preview** to resume.

**Export replay** creates a new folder containing `scene.vosc.json` and `draft.frag`. Keep those files together and open the scene in a separate VOSC renderer with `vosc --scene "/path/to/scene.vosc.json"`. Runtime OSC samples stay external to the saved files.
