# Render-machine UI views

Milestone 4 operator reference. [Design contract](../DESIGN.md) · [Codebase audit and mockup inventory](Render-machine-ui-audit.md)

The floating dock has four destinations: **Overview**, **Inspector**, **Console**, and **Browse**. Details open above the dock. **Back** returns to the originating item with its selection and scroll position. **Escape** returns directly to clean output. The scene continues rendering. Browse opens Assets by default and also offers Examples and Built-ins.

## Output and Overview

| View | First-version behavior |
| --- | --- |
| Output | The composition fills the window. A deliberate local action opens the dock. Keep a discoverable entry outside the clean render, with the exact desktop entry and shortcut confirmed in review. Capture excludes all operator UI. Errors remain available within the log's retention window and leave clean output visually undisturbed. |
| Overview | Show the scene name only while its identity is known. Use an unnamed state for ordinary OSC sessions and after a replacement with no known title. Label composed output size in pixels and observed FPS separately. Disclose camera position when enabled. Show the newest retained error since Clear view and open its exact entry while available. Console search does not affect the summary. Offer a resource jump only with verified attribution. |

Overview displays observed FPS and shows the live camera position while a camera is enabled. The app can use the startup scene title as the window title. [Debug draw order](../src/systems/RenderSystem.cpp) · [Startup window title](../src/ofApp.cpp) · [Capture behavior](Engine-api.md)

The new UI reads the local renderer directly. The optional control socket is a separate client interface. It exposes window width and height in the scene snapshot, while composed PNG capture measures backing pixels. The mockup uses composed output dimensions and a distinct unavailable state if they cannot be measured. [Scene snapshot](../src/control/EngineControl.cpp) · [Capture dimensions](../src/control/EngineControl.cpp)

## Inspector views

| View | Native behavior |
| --- | --- |
| **Globals** | Inspect geometry by source layer, shared textures, pooled shaders, and named OSC input amplitude and history. |
| **Layers** | Choose the OSC layer identifier and inspect its geometry, shader, default texture, and named texture and buffer bindings. Wide views include a layer rail. |
| **Shader** | Read the source, shader type and load state. **Bindings** opens textures and buffers. **Uniforms** reads linked GL values where available. **Variables** reads configured VOSC values. |
| **Texture** | Inspect the live output and dimensions, sampling metadata, ordered passes, and configured variables. Open the source shader where available. **Texture updates** controls the owning texture and identifies shared scope. |
| **Buffer** | Inspect the binding name, allocated texels and bytes, sampling metadata and an RGB visualization of the live buffer. |
| **Enlarged preview** | Fit the selected output to a larger panel. Texture array slice controls require both an enabled build and an allocated array with multiple slices. |

**Uniforms and variables.** Uniform values come from the currently linked GL program for supported types. The variables view displays the engine's configured values, including scalar and numeric arrays, vectors, and colors where supported. Keep those two concepts visible as separate sections. [Uniform reflection and values](../src/shader/Shader.cpp) · [Variable display](../src/operator/InspectorView.cpp)

**Texture updates.** Preserve the ability to set and clear both `isStatic` and `needsUpdate`. A static texture skips ordinary updates unless one is requested. The engine consumes a pending update and clears the flag on an eligible update. Show the pending state accurately and label these controls as changes to the running texture. Restrict them to the supported owning object. [Flag actions](../src/operator/InspectorView.cpp) · [Update behavior](../src/Texture.cpp)

**Resource identity.** Keep the visible source scope and binding name with the item. When a live command replaces or removes the selected resource, show **Resource changed** or **Resource unavailable** and return to the surviving parent. Revalidate a live texture action against the intended owning object before dispatch. Surface shared scope when an action affects a shared texture. A matching layer index after a scene reset is insufficient to restore a selection or authorize an action on that layer.

**Preview identity.** Label the rendered texture output separately from the named source and ordered pass list. The existing preview can show the last pass, a loop output, or a history frame. Present the available output even when there is no independently selectable source image. Use distinct states for an unloaded resource, an unsupported preview, and a valid black image. [Texture output selection](../src/Texture.cpp)

## Console

The existing Console draws a bounded, severity-colored log with scrolling, Copy, and Clear. The logger records message text, module, severity, elapsed time, and sequence. The current draw code checks a text filter internally and exposes no editable filter field. Current Clear empties the shared in-memory log. [Console UI](../src/Logger.cpp) · [Retained fields and limits](../src/Logger.cpp) · [Diagnostics API mapping](../src/control/EngineControl.cpp)

Keep chronological messages, copy, clear, and the ability to read older entries while new ones arrive. Show severity in text, plus elapsed time and source where available. Follow the latest messages while the operator is at the end. When they scroll back, hold their reading position and offer **Jump to latest**. **Clear view** starts the local Console and its Overview summary from the present point while retaining the bounded diagnostic stream used by other engine consumers. It clears the local row selection. **Copy entry** copies the selected message. **Copy filtered log** copies all retained rows matching the active filter and local clear boundary, including rows outside the viewport. Offer an **Errors** filter covering error and fatal severity and a text filter. Show distinct states for an empty log, no matches, and an entry that aged out. A jump from Overview reveals the exact entry even when the current filter would hide it, with a visible indication if the filter changed.

## Browse

Browse opens **Assets** by default. Search by name, narrow by category, and open an item for details. Returning with **Back** restores the originating row and scroll position. Each catalog keeps its own search and category. A saved thumbnail appears where available. Browsing and selecting leave the running scene alone.

### Examples

**Examples** discovers `scene.vosc.json` files inside the checkout's `examples/` folder or an installed renderer's staged `data/examples/` folder. Put a scene manifest and its assets together, optionally with a `preview.png` image or `scene.scd` companion. New folders and title changes appear while Browse is open. The manifest supplies the title and input declarations. The index orders known examples, and optional display metadata supplies their descriptions. Staging copies the example tree with its manifests, assets, screenshots and companions. App Store builds read their packaged `Contents/Resources/data`. [Examples index](../examples/README.md) · [Display metadata](../examples/browse.json) · [Native scene format](Native-scene-v2.md) · [Staging](../scripts/stage_assets.py)

1. Open **Browse** with **O** and select **Examples**. Search, filter, or select an example such as **Editable Scene**. The detail shows requirements and any cues that need a separate SuperCollider session. **Reveal scene file** and **Reveal companion file** locate the files on supported desktops.
2. Prepare any inputs. **Choose clip** accepts a renderer-local MOV or MP4. Cancel leaves the previous selection in place. A missing clip or unsupported codec requires another selection. For a webcam example, select its device index. VOSC checks the requested device and starts any camera permission flow only in the explicitly launched preview. Ultralight shows its page destination and requires an HTML-enabled build, matching runtime libraries, and network access. Deferred Shading requires its build capability and staged shaders. [Input validation](../src/scene/SceneFile.cpp) · [Camera startup](../src/tex/WebcamTex.cpp)
3. Select **Open in Preview Window**. **Starting** offers **Cancel**. **Ready** appears after the owned renderer sends `VOSC_SCENE_READY` for its first composed frame. Its window is titled **VOSC Preview — <scene>**. **Focus Preview** brings it forward, and **Close Preview** closes it. **Failed** includes the launch or exit diagnostic and offers **Retry** when its input and files are available. Closing Browse or returning to output leaves the preview running. Closing the primary renderer also closes its owned preview. [Scene startup](../src/main.cpp) · [Readiness](../src/ofApp.cpp) · [Preview ownership](../src/operator/PreviewProcess.cpp)
4. For a supported SuperCollider companion, use the actual `127.0.0.1` OSC port shown in Browse and evaluate the companion's **Attach** block before live cues. The **Setup** block targets a blank renderer and can reset it. Editable Scene and Sieve provide replay scripts for a distinct owned renderer. Sound Data has separate audio-analysis instructions. Preview launch uses the manifest and leaves all companion evaluation to the operator. [Companion instructions](../examples/README.md#running-the-scene-companions) · [Sound Data](../examples/sound-data/README.md)

The preview runs one independent instance through `--scene`, with a separate loopback OSC port and private control endpoint. The primary scene, OSC listener, and fullscreen mode stay in place. The preview window can cover part of a single display while focused. Separate preview windows are supported on macOS and desktop Linux builds with glibc 2.34 or newer. The Linux minimal build omits the operator UI.

### Assets

Assets mirrors the folder tree. The separate **Built-ins** view lists renderer primitives such as Box. Open a subfolder to see the files and folders inside it. Search by filename and filter by kind. Matching subfolders stay visible so results can be reached in their original location. Open a file to see its source, origin, requirements when documented, and a saved preview where available. Back restores the filter, originating row and reading position.

The Xcode checkout browses `assets/` directly. A standalone renderer browses its staged `data/` tree. Browse refreshes while the folder list is visible, so additions appear without restarting. OSC uses are defined separately for verified entries and resolve against staged data. Shader entries check required stages and includes. Penguin checks its linked material and image. [OSC use metadata](../assets/catalog/browse-assets.json) · [Shared assets and local media](../README.md#shared-assets-and-local-media) · [Staging](../scripts/stage_assets.py) · [Shader path resolution](../src/shader/Shader.cpp)

- **Details.** Show the asset's type, preview, source and requirements. Distinguish a shader used as a texture source from a shader applied to a layer program. These are separate uses with different commands.
- **Target.** Choose a current layer by its OSC identifier. Keep that choice visible. Require a new selection if the target disappears or a replacement scene reuses the same index. Show the empty layer list and what to do next when no target exists.
- **Use.** Show the OSC address, typed arguments, use, and target before copying. Generate the correct source form for that use. Texture sources use `/tex`, layer programs use `/shader`, and model or primitive sources use `/geom`. Use source references resolvable by this renderer. Mark sources requiring extra setup as such and offer a command only when it represents a valid supported use. Show copy success or failure. Copy never sends a message. [Texture commands](Texture-commands.md) · [Shader commands](Shader-commands.md) · [Geometry commands](Geometry-commands.md)
- **Clipboard format.** The address and typed arguments appear above the SuperCollider snippet for an already connected `~visuals` client. Choose the texture or layer-program use for a multi-use shader. The string escaping follows the repository's scene companion convention. [Serialization](../scripts/scene_recipe.py) · [Example usage](../scripts/generate_example_scd.py)

Keep direct asset application and replacing the current scene for a later decision about live-state replacement and failure handling. Use the Browse catalog and explicit copy or preview action for v1. [Current scene reset during startup](../src/systems/CommandSystem.cpp)

## Recommended first-version improvements

1. **Make every existing resource reachable.** Use one scrollable Inspector scoped to Globals or a layer. Keep the origin visible in resource detail, add **Back**, preserve arrow-key and pointer access, and handle a layer or resource disappearing during scene changes. Keep passive navigation from changing scene state or interrupting an active shader trial. [Input routing](../src/VOSC.cpp) · [Layer replacement](../src/systems/RenderSystem.cpp) · [Blender Outliner reference](https://docs.blender.org/manual/en/4.4/editors/outliner/interface.html)
2. **Connect the error to the investigation.** Open the Console from Overview with the matching entry selected when available. Offer a resource jump only when identity is reliable. Provide severity and text filters and copy actions. The generic logger fields alone do not establish layer or resource identity. [Logger record](../src/Logger.h) · [VS Code debugger reference](https://code.visualstudio.com/docs/debugtest/debugging)
3. **Make previews and long readouts practical.** Open shader bindings directly, scroll complete uniform and variable lists, and give array slices explicit navigation. Start with the live previews and metadata already exposed by VOSC. [Xcode bound resources](https://developer.apple.com/documentation/xcode/inspecting-the-bound-resources-for-a-command) · [Xcode texture slice previews](https://developer.apple.com/documentation/xcode/inspecting-live-resources-at-runtime)

Keep GPU frame capture, per-pixel debugging, uniform editing, and a performance timeline for a later scope review. The current FPS reading covers the first-version Overview. Optional GPU timing has a narrower measurement scope and should have its own labeling and validation if exposed later. [VOSC performance telemetry](Performance-telemetry.md) · [Xcode GPU debugging reference](https://developer.apple.com/documentation/xcode/metal-developer-workflows)

### Shortcut and behavior review

- `O` opens Overview, `C` opens Console, and `Tab` from clean output opens Inspector. Inside the overlay, Tab and Shift+Tab navigate controls. Escape returns to clean output in the current window mode.
- Texture reload, layer reset, wireframe, fullscreen, and output-mode blend shortcuts remain available on clean output. Inspector navigation is read-only. Texture updates are deliberate changes to the running scene.
- Layers use the same zero-based identifiers as OSC. [OSC target selectors](Home.md#target-selector-model)

**Mockup keyboard model.** Opening focuses the current dock destination. Left and Right move between destinations when the dock has focus. Tab and Shift-Tab move between controls. Up and Down move through lists when a list owns focus. Enter or Space activates the focused action as appropriate. Opening a detail puts focus on its first meaningful control. Back returns focus to the originating item when it still exists. Escape in the overlay returns to clean output. A native file picker handles its own Cancel or Escape and returns focus to its opener. Typing and navigating a filter field stay within the field. Show keyboard focus separately from active selection and give icon-only controls accessible names. [Apple keyboard guidance](https://developer.apple.com/design/human-interface-guidelines/keyboards/)

## First-version review criteria

| Workflow | Mockup review | Native verification |
| --- | --- | --- |
| Open and dismiss diagnostics | Reveal the dock, navigate by pointer and keyboard, and return to clean output with Escape. Check a compact window and a bright scene. | Verify that the render continues, the window mode stays the same, and captured preview pixels exclude the overlay. |
| Browse Globals and Layers | Show populated scopes with wireframes, named resources, and input history. Open a resource, go back, and recover when a selected resource disappears. | Verify inputs and previews against a working scene. Check multiple named resources with and without a default texture, including hit targets and selection after a live scene change. |
| Inspect a shader or binding | Follow a shader binding to its texture or buffer. Scroll past the initially visible uniforms and variables. Back restores the originating row and scroll position. | Verify linked shader values and configured variables, and confirm that each binding opens the correct resource. |
| Inspect or update a texture | Enlarge a 2D texture and return to detail. Treat the array slice chooser as a capability variant. Show update controls only on a resource that supports them. | Verify normal and raw buffer previews, and setting and clearing both texture-update flags. Verify slices on an array-capable build if one enters scope. Verify passive inspection leaves an active trial running and that a stale action can never change the replacement resource. |
| Find and copy an error | Open the matching Console entry from Overview. Show a resource jump when identity is known. Exercise error filtering, copy, and return to output. | Verify entry selection, real clipboard contents, retention when new messages arrive, and how Clear view interacts with other diagnostic consumers. |
| Preview an example | Find a scene and open it in a distinct preview window. Show input selection, starting and cancellation, ready and closed, and unsupported or failed recovery. | From an installed build, verify that a packaged example reaches its first composed frame. Check owned-process cleanup, GPU/device contention, and that success, failure, and close leave the primary window and its OSC state intact. |
| Find and use an asset | Filter by kind, choose an asset and target layer, review the use and command, and copy it. Show no targets, an unavailable asset, a replaced target, and copy failure. | Check that packaged source files resolve and the actual clipboard contains the correct typed target and source for texture, shader, and geometry entries. Exercise a multi-stage shader entry and escaping. Verify that Copy leaves the current scene unchanged. |

Each native row activates the exact resource it presents. The named-resource fixture covers texture and buffer binding identity with and without a default texture.
