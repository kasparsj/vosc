# VOSC render-machine UI

## Source of truth

Draft · 2026-11-22 · Approved direction: on-demand floating controls in the VOSC render window. Diagnostics may briefly cover part of the visual. One shortcut returns to clean output in the current window mode. [Codebase audit and mockup inventory](docs/Render-machine-ui-audit.md)

Milestone 1 adds the on-demand RmlUi dock, Overview, shader-error walkthrough and Console. Runtime errors are retained while the renderer continues to show clean output. Milestone 2 brings Layers and Globals into that same RmlUi surface and retires the bitmap debug Inspector. Milestone 3 adds Browse Assets with a live folder browser and layer-targeted SuperCollider snippets. [App and console routing](src/ofApp.cpp) · [Shortcuts](docs/Home.md#keyboard-shortcuts)

The Linux minimal profile omits the operator UI. The optional native control API uses a local Unix socket and captures the composed scene before operator overlays. [Build profiles](README.md#minimal-linux-build-and-render-settings) · [Engine API](docs/Engine-api.md)

Isolated macOS arm64 tests verified the dock and Console at 1024×768 and bright 640×480. A failed Layer 0 reload preserved the previous shader and animation, and both diagnostic routes copied the same retained message. Browse Examples and its preview window are planned for milestone 4.

## Brand

Quiet and precise, with the scene as the dominant visual. Use stable readings, concrete names, and direct actions. Reserve vivid accents for selection and meaningful status. Avoid decoration that competes with the image.

## Product goals

- Let an operator reveal the local UI, understand what is rendering, and return to clean output in one action.
- Let an artist locate a layer, inspect its inputs or resources, and read or copy an error while the scene continues to render.
- Let an artist discover runnable examples and reusable assets from the render window. Preview examples independently and copy a layer-targeted OSC command for a selected asset.
- Provide local operation with keyboard and pointer during single-machine rehearsal and on-site troubleshooting.

**Roadmap.** An operator surface accessible from a separate device. A second operator window on the render machine can be revisited if a show requires diagnostics to stay visible while the audience sees uninterrupted output. The present redesign covers the overlay. Scene authoring and changes to OSC or engine control are outside this draft.

## Personas and jobs

- **Visual artist rehearsing on one laptop.** Keep the composition visible while checking a layer, shader, texture, buffer, or input.
- **Operator at the render machine.** Restore a clean display quickly, understand a reported problem, and copy useful diagnostic detail.

## Information architecture

1. **Output.** The scene fills the render window. The operator opens the local UI deliberately. One keyboard action returns to output from any operator view.
2. **Overview.** Show scene identity when available, render dimensions, observed frame rate, and recent error status. Disclose camera position when a camera is enabled. Link to the relevant diagnostic or resource when it can be identified.
3. **Inspector.** Choose Globals or a layer. Open geometry previews, input histories, shaders, textures, and buffer textures from that scope. Reveal bindings, uniforms, variables, sampling metadata, and preview slices in resource detail. Keep texture-update controls within the owning resource.
4. **Console.** Keep recent messages with severity, source, and message text where available. Preserve copy and clear actions and the operator's reading position when new messages arrive. Offer filters on demand.
5. **Browse.** Open one destination with two views. **Examples** lists complete scenes and opens them in an independent preview window. **Assets** lists reusable sources and copies the OSC command for a selected layer.

The [view specification](docs/Render-machine-ui.md) maps each current capability to a proposed view and records the first-version priorities. Use its [review criteria](docs/Render-machine-ui.md#first-version-review-criteria) for the next mockup and native verification.

## Design principles

- Treat opening, inspecting, and closing the UI as observational actions. Keep scene changes deliberate and label them with their target and scope.
- Keep the composition visible where practical when the operator UI is open. Hide operator chrome on explicit return to output.
- Record runtime errors while the UI is hidden. Show recent errors when it next opens. Fatal startup failures follow the existing diagnostics path.
- Keep fullscreen independent of panel visibility so the operator can return to clean output without changing display mode.

## Visual language

**Reference.** The retired debug Inspector used white bitmap text drawn over the render, square wire outlines, and resource thumbnails. Yellow outlines identify clickable shaders. The input amplitude history uses a magenta trace. Shader and texture detail views use compact labels and aligned columns. [Inspector](https://github.com/kasparsj/vosc/blob/6b381d0b5e1c4f26424ad5737a97f1c9b7d30b0e/src/inspector/Inspector.cpp) · [Shader detail](https://github.com/kasparsj/vosc/blob/6b381d0b5e1c4f26424ad5737a97f1c9b7d30b0e/src/inspector/ShaderInspector.cpp) · [Texture detail](https://github.com/kasparsj/vosc/blob/6b381d0b5e1c4f26424ad5737a97f1c9b7d30b0e/src/inspector/TextureInspector.cpp)

**Selected direction.** Use the floating controls from Apple's tvOS media and visionOS playback references as the visual starting point. Navigation sits in a small translucent dock near the bottom center of the render. The active view appears in a separate, more tinted surface above it. The dock's selection moves between views with the arrow keys. Escape returns to unobstructed output. [Current design in Figma](https://www.figma.com/design/1dWhdS4r5BQkPGDMGvX867?node-id=5-117) · [tvOS material example](https://developer.apple.com/design/human-interface-guidelines/materials#tvOS) · [visionOS playback](https://developer.apple.com/documentation/visionos/building-an-immersive-media-viewing-experience)

Use a soft highlight and local background diffusion to give the dock depth. Increase tint behind detailed text so it stays readable over bright imagery. Keep the highlight motion brief and respond to reduced-motion and reduced-transparency preferences where available. The mockup's glass treatment is a browser approximation and the native implementation approach is open.

**Earlier research.** The game-interface studies explored four ways of presenting the overlay through visual composition, selection, and response. All used the same saved Organic Trails frame and sample data. Each supported pointer and arrow-key selection and returned to clean output with Escape. [Study screenshot](docs/mockups/inspector-style-directions.png)

| Direction | Research and VOSC application |
| --- | --- |
| Vector Probe | Tetsuya Mizuguchi describes *Rez Infinite* as an experiment in creating a sensory response through a simple interaction. Extend VOSC's existing yellow shader outlines into a spatial selection system. Moving the operator's selection moves the reticle and updates the readout. [Mizuguchi on Area X](https://blog.playstation.com/archive/2016/09/20/why-the-all-new-area-x-might-be-the-most-exciting-level-of-rez-infinite/) |
| Signal Run | Drool makes rhythm and forward momentum the organizing principles of *Thumper*. Here a diagonal track makes earlier input, a shader error, and the next rendered frame readable in sequence. A frame-indexed event history is a prototype assumption. [Drool press kit](https://thumpergame.com/presskit/) |
| Intermission | Emeric Thoa describes *Furi*'s deliberate focus and colorful, surreal art direction. Here one bold color plane and a strong selection hierarchy make Overview, Inspector, and Console read like a game menu. The render keeps running. [The Game Bakers on Furi](https://www.thegamebakers.com/furi-and-creating-memorable-moments/) |
| Field Kit | Lucas Pope describes using a constrained 1-bit palette in *Return of the Obra Dinn* and tuning its sharpness for sustained play. Here the constraint applies to the inspector chrome and original resource icons. The operator navigates a resource grid and reads the selected item's details. [Lucas Pope on Obra Dinn's art](https://blog.playstation.com/archive/2019/10/17/lucas-pope-on-return-of-the-obra-dinns-art-style/) |

Judge the minimal mockup by how quickly an operator can find a shader error and return to clean output. Keep status labels explicit. Check contrast over a bright scene and compare how much of the visual remains visible at both window sizes.

## Components

| Component | Required variants or states |
| --- | --- |
| View switcher | Overview, Inspector, Console, Browse, active view and keyboard focus |
| Status readout | Scene name when known, output dimensions, observed FPS, unavailable reading |
| Layer list and resource detail | Selected, no selection, empty, resource changed, resource unavailable |
| Console and log row | Severity and source when available, empty, copied, reading history |
| Browse catalog and item detail | Examples and Assets, search, category, selected item, saved preview or placeholder, requirements, empty or unavailable |
| Browse actions | Preview input preparation, starting, ready, cancelled, closed and failed, asset target selection, Copy OSC command with feedback |
| Return to output | Visible action and corresponding keyboard shortcut |

Apply the chosen visual direction across the views with shared typography, spacing, and status colors.

## Accessibility

The target is legible text on a stable background, visible keyboard focus distinct from the selected state, and a keyboard path through diagnostics and Browse. Input consumed by the UI stays with the focused control. Keep shortcut hints visible where they help. Design an opaque appearance for reduced transparency and a still appearance for reduced motion. Confirm native accessibility semantics and assistive-technology support when selecting the toolkit. [Keyboard behavior](docs/Render-machine-ui.md#shortcut-and-behavior-review)

## Responsive behavior

Design for pointer and keyboard in windowed and fullscreen use. Show the scene alongside diagnostics when space allows. At smaller window sizes, prioritize the active view and let its content scroll. Keep the return action and current view visible. Check the normal 1024×768 startup size, a smaller supported window, fullscreen, and HiDPI. Confirm the smallest useful window during mockup review.

## Interaction states

| State | Operator experience |
| --- | --- |
| Output only | Scene fills the window. Runtime errors remain available in the console. |
| Rendering with UI open | Scene continues rendering. Overview shows observed values with units. |
| No named scene or no inspectable resources | Show the available runtime status. Explain missing identity or empty resource lists without implying a fault. |
| Recoverable runtime error | Show concise context and provide access to the console from the open UI. Preserve the existing render outcome. |
| Reading unavailable | Give the reason when available. Avoid substituting a zero for missing performance data. |
| Example preview | Show required inputs before launch. Indicate starting, ready, or failed in Browse. The active render continues independently. |
| Asset ready to use | Show the selected layer and source role before copying. If the asset or target is unavailable, explain the reason at the action. |
| Fatal startup failure | The existing startup path reports the cause to stderr and exits. An in-window recovery experience would require a separate scope decision. [Startup path](src/main.cpp) · [App startup errors](src/ofApp.cpp) |

## Content voice

Use the renderer's terms consistently, including **Scene**, **Layer**, **Shader**, **Texture**, **Buffer**, and **Console**. Use **Example** for a complete scene and **Asset** for a reusable source. Show units with readings. Lead error messages with what happened and the affected resource when known. Keep engine diagnostics available to copy.

## Implementation constraints

- Keep operator chrome out of the composed scene and engine preview capture. Validate output and preview with the local UI visible.
- Preserve the existing OSC and local control contracts. Passive UI interactions should leave scene state and active shader trials alone. Current input routing can interrupt trials, so this requires a check during implementation. [Input routing](src/VOSC.cpp) · [Engine API](docs/Engine-api.md)
- Use existing measurements where appropriate and label their scope. The optional GPU timing has a narrower scope than whole-app frame time. Assess overhead on a representative scene with the UI hidden and visible. [Performance telemetry](docs/Performance-telemetry.md)
- The initial design assumes the native desktop build with UI support. Confirm rollout for other build profiles before implementation. The spec leaves the toolkit choice open.

### Mockup review

Use a representative VOSC scene as the backdrop. Show clean fullscreen output, then open the overlay and check the scene and frame rate in Overview. In Inspector, select an affected layer and inspect its shader. In Console, find and copy the diagnostic, then use one shortcut to hide the entire overlay while fullscreen remains active. If the engine supplies no layer attribution, show the error with the context it provides. Include an empty or unavailable state and a constrained window. Approve the workflow and which existing diagnostic functions are required before updating code.

For Browse, review opening a working example in a separate preview window, resolving a missing input or build requirement, and finding an asset and copying its command for a chosen layer. The same floating dock and Back behavior apply to both catalogs. Use the [mockup inventory](docs/Render-machine-ui-audit.md#mockup-inventory) to cover the required views and their edge states.

The floating-controls study established the look and the basic shader-error walkthrough. The Current design page in Figma is the visual reference for the wide and bright compact dock, Console, Inspector, and Browse Assets. The historical density review records simulated Inspector and Browse interactions. Browse Assets uses the existing native shell and mirrors the asset folder hierarchy. The live scene replaces the design backdrop, and runtime availability and clipboard results replace the mockup's fixture controls. [Wide Console](https://www.figma.com/design/1dWhdS4r5BQkPGDMGvX867?node-id=5-209) · [Bright compact Console](https://www.figma.com/design/1dWhdS4r5BQkPGDMGvX867?node-id=5-2494) · [Density review](docs/mockups/render-machine-overlay-density-review/verification.md)

## Open questions

- [ ] Product review: Does the Overview need only scene identity, output size, observed FPS, and recent errors, or is another reading essential during a show? This determines the default view.
- [ ] Product review: Which preserved Inspector actions belong on the first level? Confirm shortcut assignments and the placement of scene-changing texture controls.
- [ ] Product review: Is the first release macOS only? This determines which build profiles need overlay support.
- [ ] Product review: Where does the operator discover and open the overlay from clean output? Confirm the opener and direct Console shortcut against the focus model.
- [ ] Product review: Confirm the one-preview-at-a-time design and how a windowed preview behaves when the primary render is fullscreen on the only display.
- [x] Assets copy a SuperCollider snippet for an already connected `~visuals` client.
