# Direct-download macOS release

## Editable data

The DMG contains `vosc.app`. Open the app from the image or copy it to Applications. On first launch it prepares `~/Library/Application Support/VOSC/data/`. Press **O**, then choose **Browse → Open Data Folder** from Assets or Examples. Use an editor or Finder to change the files in that folder. Keep an example's scene file and relative assets together.

A newer app refreshes stock files that still match the version VOSC installed. Edits, added files and intentionally removed stock stay as you left them. VOSC compares content, so restoring a file to the exact recorded stock bytes makes it eligible for a later stock refresh. If an update reports a recovery copy, use the path in the message to retrieve an edit from an editor that had the original file open. VOSC keeps displaced stock under `~/Library/Application Support/VOSC/.vosc-managed/recovery/` until you remove it. In Finder, use **Go → Go to Folder** to inspect the recovery folder.

For a setup or update error, follow the path shown in the message and choose **Retry** when it is accessible. If another VOSC instance is using the data folder, quit it before retrying an update. If the whole data folder was moved or removed after setup, restore it from your backup to the same path.

### Bring over edits from v0.2.0

1. Launch the new app once and use **Browse → Open Data Folder**. Keep that Finder window open and quit VOSC.
2. Back up the old `data/` folder that sat beside the v0.2.0 app. Work from the backup and select only files you want to transfer.
3. Copy shared assets into the corresponding subfolders in the new data folder. Keep their relative paths and dependencies. For an edited example, copy its complete folder with its scene and assets. If the name is already present, give the copied example a new folder name.
4. If a shared file already exists, choose **Stop** in Finder and keep the destination by default. To use the old edit at the same path, first copy the new original to a separate backup outside the VOSC folder, then replace that individual file.
5. Reopen the app and load the transferred example or asset from Browse. Keep the old backup until you've checked your edits.

## Assemble the release

Build from the integrated release revision using the intended bundle version, pinned openFrameworks dependencies and the operator UI. Use fresh output folders and a stock generation greater than the last distributed managed release. One generation always identifies one stock manifest.

```sh
python3 scripts/build_macos_profile.py --of-root "$of_root" --addon-sdk "$addon_sdk" \
  --output "$build_dir" --configuration Release -- VOSC_OPERATOR_UI=1 CODE_SIGNING_ALLOWED=NO
python3 scripts/stage_macos_release.py "$build_dir/of-0.12.1/Release/products/vosc.app" \
  "$stage_dir" --addon-sdk "$addon_sdk" --generation "$stock_generation"
```

The staging command compares the sealed stock inputs against the committed assets and examples and the pinned deferred shaders. It leaves a new staging directory containing just the app. Sign nested code and then the app using the Developer ID Application identity and hardened runtime. Verify the app, create the compressed read-only image from the staging directory, sign the image, and verify it. The existing app build embeds its nested libraries under `Contents/Frameworks`.

```sh
app="$stage_dir/vosc.app"
if test -d "$app/Contents/Frameworks"; then
  find "$app/Contents/Frameworks" -depth \( -type f -name '*.dylib' -o -type d -name '*.framework' \) \
    -exec codesign --force --sign "$developer_id" --options runtime --timestamp '{}' \;
fi
codesign --force --sign "$developer_id" --options runtime --timestamp "$app"
codesign --verify --deep --strict --verbose=2 "$app"
hdiutil create -fs HFS+ -volname "$volume_title" -srcfolder "$stage_dir" -format UDZO "$dmg"
codesign --force --sign "$developer_id" --timestamp "$dmg"
codesign --verify --strict --verbose=2 "$dmg"
hdiutil verify "$dmg"
```

For an authorized notarization, submit the DMG with `xcrun notarytool submit "$dmg" --keychain-profile "$notary_profile" --wait`. Record the submission ID and review the notary log with `xcrun notarytool log`. Staple and validate the DMG with `xcrun stapler staple "$dmg"` and `xcrun stapler validate "$dmg"`. Check Gatekeeper policy for the final DMG with `spctl -a -t open -vvv --context context:primary-signature "$dmg"`. On macOS 14 or newer, also assess the mounted app with `syspolicy_check distribution /path/to/vosc.app`. Record the commit, bundle version, generation, stock manifest hash and final post-staple DMG SHA-256.

## Automated smoke

Run on a logged-in, unlocked macOS desktop. For release qualification use the final Developer ID signed, notarized and stapled DMG. Record its SHA-256 independently and keep the result for those exact bytes.

```sh
python3 scripts/test_macos_dmg.py /path/to/vosc-macos-universal.dmg \
  --sha256 <expected-sha256> --output /path/to/new-smoke-results
```

The command mounts the image read-only, lists the payload, verifies the app signature and slices, and compares the stock manifest with the bundled data. It uses disposable homes after checking Foundation's path resolution. It runs Sieve from the mounted app and from the copied app after ejecting, including a fresh copy launch. It checks the expected OSC pixel change, an edited relaunch, and a missing-texture diagnostic. It saves `result.json`, renderer logs and PNGs. The automated run can also be used for local development with an ad hoc signed app. Record its signing status separately from the final Gatekeeper and Finder checks.

## Finder checklist

1. Use a clean test account or clean desktop snapshot with no VOSC data or VOSC instance running. A fresh machine or restored snapshot provides the strongest evidence for Gatekeeper. Record prior machine trust history when using a fresh account on an existing machine.
2. Download the exact final DMG using a browser. Record the browser and version, SHA-256 (`shasum -a 256`), macOS version/build (`sw_vers`), architecture (`uname -m`) and quarantine metadata before first opening. Match the digest to the automated report. Record any subsequent quarantine change during normal use.
3. Open the DMG in Finder and double-click `vosc.app`. Record the normal first-launch prompt and whether a responsive window opens. A security override fails this check.
4. Press **O** and choose **Browse → Examples → Sieve → Load Example**. Record the patterned image with a screenshot. Use **Reveal scene file** and **Open Data Folder** to verify the active Application Support paths.
5. Quit VOSC. In Finder, copy only `vosc.app` into a clean writable folder. Eject the DMG and verify it is gone, then double-click the copied app and repeat the Sieve and folder checks.

Record separate native Intel verdicts for the automated and Finder checks. A Universal build can contain an Intel slice without having been run on native Intel hardware.

For the historical [v0.2.0 release](https://github.com/kasparsj/vosc/releases/tag/v0.2.0), the published digest was `f42d0bf489a9f1b2e095df239f572306188579ca525bcdf7641ada6cb1608404`. Its automated Apple Silicon smoke covered the old adjacent and bundled layouts. The clean-account Finder journey remained open.
