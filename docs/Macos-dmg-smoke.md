# macOS DMG smoke test

Run the smoke command from a logged-in, unlocked macOS desktop on a trusted network. Use the final signed, notarized and stapled DMG intended for upload. Record its SHA-256 independently and keep the result for those exact bytes.

```sh
python3 scripts/test_macos_dmg.py /path/to/vosc-macos-universal.dmg \
  --sha256 <expected-sha256> --output /path/to/new-smoke-results
```

For the [published v0.2.0 baseline](https://github.com/kasparsj/vosc/releases/tag/v0.2.0), the expected SHA-256 is `f42d0bf489a9f1b2e095df239f572306188579ca525bcdf7641ada6cb1608404`.

The command mounts the image read-only and runs Sieve with adjacent data and with bundled data in an app-only copy. It verifies a composed frame, the expected pixel change after one OSC cue, and the missing-asset diagnostic from a disposable scene. A passing run exits 0 and saves `result.json`, renderer logs and before/after PNGs in the output directory.

## Finder checklist

1. Use a fresh macOS test account or clean desktop snapshot with no prior approval of the artifact and no VOSC instance running. Download the same DMG in a browser and leave quarantine metadata intact. Record the browser, SHA-256 (`shasum -a 256`), macOS version/build (`sw_vers`) and architecture (`uname -m`). Match the digest to the automated report.
2. Open the DMG in Finder and double-click its `vosc.app`. Record any first-launch prompt and whether a responsive window opens through the normal launch flow. A security block requiring a manual override fails this check.
3. Press **O**, then choose **Browse → Examples → Sieve → Load Example**. Pass if the example is listed and the same window shows the patterned image. Record the outcome and screenshot. **Reveal scene file** can show whether a translocated launch used the bundle's copy.
4. Quit VOSC. In Finder, copy only `vosc.app` into a clean writable folder with no adjacent `data/`, then eject the DMG. Double-click the copied app and repeat the Examples check. Record the result for each layout.

Apple Silicon is sufficient for the first pass. Track native Intel automated and Finder coverage separately.
