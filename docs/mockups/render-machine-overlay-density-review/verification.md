# VOSC overlay density review

This report records the retired offline HTML prototype. **O** and **Open overlay** opened it in Chrome. **Inspector density** selected a nine-layer fixture, **Console · 512 entries** showed an expired Overview link and the retention boundary, and **Bright scene** changed the backdrop. The screenshots below preserve those simulated states. [Current design in Figma](https://www.figma.com/design/1dWhdS4r5BQkPGDMGvX867?node-id=5-117)

The density prototype was built in the `main` checkout at `2430a193fc0c94400284726004d343911698a29e`. The initial density review used HTML blob `683c23b39c99f5e714922ca2b20ca8f5288cb032`. The Tab comparison used blob `623ae12b1e3a60f147392a80dd4e118afa37c9f5`. The final O variant used blob `80fa0939f3dfb2c62003cc95b5bd781bff69d9fc`. Earlier captures were preserved.

## Browser verification

Playwright CLI drove Chrome with pointer and keyboard at 1024 × 768 and 640 × 480. The core checks passed at both sizes over the saved dark frame and the bright stress backdrop.

The current shortcut passed in offline Chrome at both sizes. O opened and reopened the overlay, typed normally in Browse search, and left Tab and Shift-Tab available for focus navigation. Escape restored clean output with the same stage bounds.

| Area | Verified behavior |
| --- | --- |
| Inspector | Nine zero-based OSC layers. Opened `srctex`, `sieve`, `colors`, and `tex` with the default texture present and absent. Layer and Shader links opened the same binding identities. Back restored the origin, focus, and vertical scroll from detail and enlarged preview. |
| Readouts and resources | Reached row 26 in both the linked GL uniforms and configured VOSC variables tables. Opened Globals wireframe, `analysis` history, shared `particlesData`, and its source shader. Exercised owner flag changes, valid black output, the separate array-capable fixture, and expiry back to a surviving parent. Raw buffers had no owner controls. |
| Browse | Confirmed 20 examples, 107 fragment paths, and two additional asset samples. Reached the last examples and fragment rows. Search, category, zero-result recovery, and catalog switching preserved the expected query, selection, and scroll. Verified the simulated typed shader and texture commands with explicit layer targets. Reference-only items, missing targets, replaced targets, and unavailable sources omitted command copy. |
| Preview | Exercised start, cancel, ready, focus, return to primary, close, failed launch and retry. Checked local-clip selection and cancellation, camera unavailable and denied, and the default-build HTML restriction. |
| Console | Overview and Shader reached the same diagnostic, `#184`, even under a hiding filter. Checked visible filter-reset feedback, simulated copy, empty and no-match states, an expired link, and an unattributed error. A new entry held the paused scroll position and evicted the oldest row at 512 entries. Jump to latest resumed following, and Clear view reset Overview and the Console view. |
| Keyboard and output | Checked visible focus, dock arrows, Tab and list arrows, keyboard activation, filter input key handling, and focus return. Escape restored clean output and identical render-stage bounds from enlarged resources, asset details, diagnostics, and the mock Preview Window. |
| Offline | Loaded the local `file://` URL using Playwright with Chrome offline. The saved frame decoded and the Browse and Escape flow worked with zero external requests or JavaScript errors. |

The browser exercises used the Playwright CLI wrapper's `run-code --filename` with temporary scripts. The offline smoke used the installed Playwright browser library. The HTML also passed a whitespace check against the original with `git diff --no-index --check`. Readouts, texture controls, copy and preview lifecycles are browser simulations. The 107 fragments are checkout inventory. The copy-enabled samples were checked against local commands, source files, and example recipes.

## Impeccable review

Single-context design review and detector pass, consistent with the direct local execution route.

- **Density.** The wide Layer view benefits from the side selector. The compact view keeps Back, the current view, and Return to output fixed above one scroller. At 640 × 480, Browse has 141 px of scrolling content below its persistent filters, so the first screen shows only about three fragment rows. Search and category filters do most of the work at this size.
- **Readability.** The dark panel keeps text legible over the bright render. Larger resource thumbnails and 12 px secondary row text make the binding source and reference status easier to scan. The unscaled 4,356 × 1 buffer was illegible, so the prototype explicitly labels its preview as a vertically expanded sample. The live detector cleared its 107 repeated undersized-text labels after the text adjustment.
- **Scene visibility.** The dock and panel leave approximately **58%** of the stage outside their bounding boxes at 1024 × 768 and **39%** at 640 × 480 in dense Layer, Shader and Browse. The compact review header raised that figure from 22% in the first implementation capture. Compact Browse remains the tightest case because two rows of filters consume panel height. Short details, such as a raw buffer, surrender unused height to the scene.
- **Detector limits.** The source detector flagged the stage's zero padding and the radial gradient used for the bright stress backdrop. Both are intentional in the existing shell. The injected browser detector reported one remaining page-level finding without a labeled element. Browser detector injection was temporary.

## Screenshots

| View | Capture |
| --- | --- |
| Dense Inspector | [Layer at 1024](01-layer-1024.png) · [Bright layer at 640](10-bright-layer-640.png) |
| Shader | [Bindings](02-shader-bindings-1024.png) · [End of uniforms list](03-shader-uniforms-1024.png) |
| Resources | [Texture](04-texture-1024.png) · [Buffer](05-buffer-1024.png) · [Enlarged preview](06-enlarged-1024.png) |
| Browse | [107 fragment results](11-bright-assets-640.png) · [Asset command](07-asset-command-1024.png) · [20 examples](13-bright-examples-640.png) · [Example detail](14-bright-example-detail-640.png) · [Preview Window](15-bright-preview-window-640.png) |
| Compact Browse, earlier Tab comparison | [Examples](16-compact-browse-examples-tab.png) · [Fragments](17-compact-browse-assets-tab.png) |
| Diagnostics and output | [Paused Console](12-console-paused-1024.png) · [Compact diagnostic](09-bright-diagnostic-640.png) · [Compact clean output](08-bright-clean-640.png) |
