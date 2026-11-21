# GPU backend research

Research conducted 17–19 November 2026. This records the investigation and
recommendations from the project discussion; it is not an approved migration plan.
External project status is a dated snapshot and must be refreshed before adoption.

## Findings and recommendation

- Retain the existing OpenGL renderer for now. Migration is a medium-priority
  architectural investigation, rather than an urgent replacement justified by
  the evidence collected.
- For incremental WebGPU compute or generated texture sources, investigate
  **ofWorks/ofxDawn**. It keeps OF's OpenGL renderer and introduces a separate
  Dawn/Metal device with a shared-texture bridge.
- For reducing dependence on Apple's OpenGL implementation, investigate
  **maintained upstream ANGLE on Metal**. This still requires a desktop-GL-to-GLES
  compatibility audit. It is distinct from adopting the older MetalANGLE fork.
- The Dawn renderer proposed in **openFrameworks PR #8577** is not currently a
  suitable replacement for VOSC: essential shader, framebuffer, and instancing
  functionality is missing from its OF integration.
- No candidate was built or benchmarked against VOSC. No universal performance
  improvement, graphics parity, or installation reliability was established.

These are engineering judgments based on the evidence below. Raise migration
priority if reproducible driver failures affect VOSC, measurements establish a
backend bottleneck, required features exceed GL 4.1, or Apple announces removal.

## What openFrameworks supplies to VOSC

OF is embedded in the renderer and resources, rather than being a replaceable
window wrapper:

| Requirement | Repository evidence | Migration implication |
| --- | --- | --- |
| Desktop OpenGL 4.1 | [Config.h](../src/Config.h), [main.cpp](../src/main.cpp) | GLES and WebGPU require compatibility work, not just a backend flag. |
| Texture/pixel interfaces | [BaseTex.h](../src/tex/BaseTex.h) inherits OF interfaces | New source types must satisfy the existing resource contract. |
| Custom GLSL, source loading, file watching, Shadertoy | [Shader.cpp](../src/shader/Shader.cpp) | Preserve compilation, bindings, diagnostics, and reload behavior. |
| Desktop GLSL and geometry shaders | [passthru.vert](../bin/data/shaders/passthru.vert), [Grass.geom](../bin/data/shaders/grass/Grass.geom) | Audit shader stages and language features; an ES port is not automatic. |
| Instanced geometry | [Geom.cpp](../src/Geom.cpp) | A generic renderer without instancing loses an existing capability. |
| Feedback and framebuffer operations | [FboPingPong.cpp](../src/FboPingPong.cpp), [ShaderPass.cpp](../src/shader/ShaderPass.cpp) | Preserve formats, attachments, history, and sampling semantics. |
| Post-processing/deferred shading | [RenderSystem.hpp](../src/systems/RenderSystem.hpp) | OF addon compatibility is part of the migration. |
| Video/image/webcam sources | [Texture.cpp](../src/Texture.cpp), [texture implementations](../src/tex) | Validate media upload, texture orientation, and composition. |
| GPU timing | [PerformanceOpenGl.cpp](../src/control/PerformanceOpenGl.cpp) | Alternative backends need equivalent timing/admission evidence. |

Preserving OSC command names alone does not preserve rendering or musical timing.
The existing command, input, resource, and render system separation is useful,
but does not establish a framework-independent engine.

## Alternative framework survey

The initial survey identified these options. The assessments are architectural
fit judgments, not results from ports or comparative performance tests.

| Option | Reason to consider it | Main cost or limit |
| --- | --- | --- |
| [Cinder](https://libcinder.org/about) | Closest C++ creative-coding counterpart: graphics, media, cameras, audio, OSC. | OF code/addons still need porting; its documented graphics layer is OpenGL-oriented. |
| [bgfx](https://bkaradzic.github.io/bgfx/overview.html) with a C++ application shell | Retain C++ with Metal/Vulkan/Direct3D/OpenGL rendering abstraction. | Bring the engine/framework: media, resources, effects, and authoring conveniences remain project work. |
| [SDL3 GPU](https://wiki.libsdl.org/SDL3/CategoryGPU) | Modern graphics/compute API alongside application/window facilities. | Lower-level foundation; requires building creative-engine facilities. |
| [wgpu](https://wgpu.rs/) | Portable native/browser graphics and compute; especially relevant to a Rust rewrite. | New rendering/resource integration; Rust adds a language port, although C/C++ bindings exist. |
| [Godot](https://docs.godotengine.org/en/stable/tutorials/rendering/renderers.html) | Engine/editor and modern rendering maintained upstream. | Adopt its lifecycle and resource model; [shader adaptation](https://docs.godotengine.org/en/stable/tutorials/shaders/shader_reference/shading_language.html) is required. |
| [TouchDesigner](https://derivative.ca/UserGuide/OSC_In_DAT) | Strong audiovisual authoring fit with built-in OSC. | Changes implementation toward node networks/scripting; [licensing](https://derivative.ca/UserGuide/Licensing) and target-platform requirements apply. |
| [Three.js](https://threejs.org/docs/pages/WebGPURenderer.html) / TypeScript | Browser delivery and web-based editing; WebGPU renderer with WebGL2 fallback. | C++/shader integration port and an OSC transport bridge for browser use. |
| [TrussC](https://github.com/TrussC-org/TrussC) | OF-inspired C++ framework built on Sokol, advertising Metal and familiar APIs. | Separate framework, not an OF fork or compatible upgrade; feature parity is untested. |

The survey did not establish a reason to rewrite solely to leave OF. Existing
framework improvements should be assessed before committing to a full port.

## OpenGL on current Macs

### Apple support and recent compatibility events

- Apple deprecated OpenGL in macOS 10.14. Its guidance says OpenGL remains
  available on Apple silicon and recommends migrating to Metal. No announced
  OpenGL removal date was found in the reviewed sources.
  [Apple silicon migration guidance](https://developer.apple.com/documentation/apple-silicon/porting-your-macos-apps-to-apple-silicon)
- macOS 26 removed **AGL**, the obsolete Carbon presentation API. Apple's notes
  explicitly distinguish this from OpenGL, which remains in the SDK.
  [macOS 26 release notes](https://developer.apple.com/documentation/macos-release-notes/macos-26-release-notes)
- OF's AGL link failure was addressed upstream. Removing an obsolete framework
  dependency is a build compatibility fix, not a requirement to port the renderer.
  [OF issue #8473](https://github.com/openframeworks/openFrameworks/issues/8473)
- Reports of blank OpenGL windows on macOS 15.8.2 RC1/RC2 were a real regression.
  The original reporter confirmed a fix in RC3, build 24H230. These reports do
  not establish that all current Macs have broken OpenGL or that Apple removed it.
  [Apple developer forum report and resolution](https://developer.apple.com/forums/thread/850550)
- GLFW documents deprecated macOS OpenGL support and maintenance limitations.
  This supports caution around OS updates, not a specific VOSC failure claim.
  [GLFW compatibility documentation](https://github.com/glfw/glfw/blob/master/docs/compat.md)

### Local capability probe, 19 November 2026

A temporary CGL context was created and destroyed through Python `ctypes`, using
the system OpenGL framework, an accelerated pixel format, and the 4.1 core
profile. No VOSC process was launched, restarted, or modified.

| Query | Observed result |
| --- | --- |
| `sw_vers` | macOS 27.2.1, build 26B131 |
| Architecture | arm64 |
| CGL pixel-format/context/current-context operations | All returned success (`0`) |
| `GL_VENDOR` | `Apple` |
| `GL_RENDERER` | `Apple M3 Pro` |
| `GL_VERSION` | `4.1 Metal - 91.7` |
| `GL_SHADING_LANGUAGE_VERSION` | `4.10` |
| `GL_TIME_ELAPSED` / `GL_QUERY_COUNTER_BITS` | 32 bits, `GL_NO_ERROR` |

This proves local context creation and capability queries. It does not prove
window presentation, shader correctness, sustained timing, or VOSC performance.
The Metal renderer string also means that another Metal-backed API should not
be presumed faster merely because it uses Metal.

The VOSC connector's read-only capability/performance queries returned
`adapter_engine_unavailable` with `engine socket does not exist`. No live scene
benchmark was obtained. Timer counter availability is not a completed timer-query
measurement and does not establish protected-authoring acceptance.

The local legacy build helper already strips AGL/QTKit link flags. Its documented
reuse of a prebuilt OF Debug library is not a clean rebuild of OF/dependencies.
[Legacy build helper](../scripts/build_macos_legacy.py)

## OF's Metal/WebGPU work and PR consensus

### Upstream and related experiments

- [PR #8577](https://github.com/openframeworks/openFrameworks/pull/8577) adds
  opt-in Dawn and MetalANGLE backends while retaining existing defaults.
- [Issue #7761](https://github.com/openframeworks/openFrameworks/issues/7761)
  tracks a macOS Metal backend request, including ANGLE discussion.
- [PR #6981](https://github.com/openframeworks/openFrameworks/pull/6981) is an
  older iOS MetalANGLE prototype.
- [ofxMetal](https://github.com/yusuketomoto/ofxMetal) is an older addon; its last
  repository push was in 2019 when checked. It is not evidence of a maintained
  replacement OF renderer.

### PR #8577 status snapshots

| Checked | Evidence | Interpretation |
| --- | --- | --- |
| 17 November | Open/unmerged, non-draft; milestone 0.13.0 without a due date. Head `7cab6c66af8cf2cff0abacedd0f0af62efa60927`; 29 successful checks. | Concrete development and release-target intent, not a delivery commitment. |
| 17 November | One `COMMENTED` review, no approvals or changes-requested reviews. Four discussion comments, all by the author. | Too little independent review to establish merge consensus. |
| 19 November | Updated 18 November; still open/unmerged, milestone 0.13.0. Head `5c65ce5d1cd0872e537d4ddaabd1bfc771227ffe`; no check runs returned for that SHA. Same sole commented review. | Earlier passing checks do not cover the new revision. No demonstrated acceptance or imminent merge. |

The review asks whether font-atlas pixels should be populated consistently across
renderers. It is not an approval, rejection, or architectural endorsement.
[Review comment](https://github.com/openframeworks/openFrameworks/pull/8577#discussion_r4360981846)

The initial discussion understated the roadmap signal: the 0.13.0 milestone is
real. The corrected conclusion is an active proposal with a release target,
awaiting substantive review/acceptance and without a delivery date.

### MetalANGLE versus upstream ANGLE

The PR's `OF_USE_ANGLE=1` path uses MetalANGLE and MGLKit, exposing OpenGL ES
rather than a desktop GL 4.1 implementation. VOSC's desktop GLSL, shader stages,
texture targets/formats, and addon GL calls require an actual compatibility audit.
[Pinned PR window source](https://github.com/openframeworks/openFrameworks/blob/5c65ce5d1cd0872e537d4ddaabd1bfc771227ffe/libs/openFrameworks/app/ofAppMGLWindow.mm#L296)

The original [MetalANGLE project](https://github.com/kakashidinho/metalangle#readme)
says active development stopped in 2023 and directs new development to
[upstream ANGLE](https://github.com/google/angle#readme). The PR's benchmark logs
identify the older MetalANGLE revision `ec925142edeb`. Its separately benchmarked
official ANGLE/EGL window is explicitly not included in the PR.

Therefore, investigating maintained ANGLE-on-Metal and adopting this PR's
MetalANGLE integration are different dependency decisions.

### Limitations of OF's proposed Dawn renderer

These findings concern the OF integration at the 19 November PR head, not
fundamental limitations of Dawn or WebGPU:

- Generic shader objects and GPU instancing are explicitly unimplemented.
  [Renderer header](https://github.com/openframeworks/openFrameworks/blob/5c65ce5d1cd0872e537d4ddaabd1bfc771227ffe/libs/openFrameworks/graphics/ofWebGPURenderer.h#L20)
- `ofShader::begin/end` still depend on the GL renderer.
  [Shader implementation](https://github.com/openframeworks/openFrameworks/blob/5c65ce5d1cd0872e537d4ddaabd1bfc771227ffe/libs/openFrameworks/gl/ofShader.cpp#L845)
- `ofFbo` retains a GL-renderer path.
  [Framebuffer implementation](https://github.com/openframeworks/openFrameworks/blob/5c65ce5d1cd0872e537d4ddaabd1bfc771227ffe/libs/openFrameworks/gl/ofFbo.cpp#L882)
- Camera bind/unbind are empty, and the video path reads CPU pixels and uploads
  them. Existing VOSC camera/media behavior needs separate validation.
  [Renderer implementation](https://github.com/openframeworks/openFrameworks/blob/5c65ce5d1cd0872e537d4ddaabd1bfc771227ffe/libs/openFrameworks/graphics/ofWebGPURenderer.cpp)

PR benchmarks are author-supplied, workload-dependent demonstrations. Some use
custom WGSL pipelines or local changes outside the PR. iOS results are simulator
measurements with differing framebuffer sizes. They do not establish VOSC
performance or equivalent feature coverage.

## PR Dawn backend versus ofWorks/ofxDawn

Addon source checked at `546aba0c6206b32dbbfd9a19fd3ec13f2086fa91` on 19 November.

| Concern | PR #8577 Dawn backend | ofWorks/ofxDawn |
| --- | --- | --- |
| Purpose | Replace OF window/render backend with WebGPU/Metal. | Add a separate Dawn device alongside the existing renderer. |
| Presentation | Metal-backed window through `CAMetalLayer`. | WebGPU output is sampled by OF's OpenGL renderer. |
| Existing VOSC GL behavior | Requires substantial adaptation. | Can stay in the existing renderer; addon interoperability still needs testing. |
| Custom GPU work | Generic OF shader interface is missing. | WGSL module helper and direct device/queue access; implemented compute/render example. |
| Compute example | Not the primary generic renderer interface. | Particle simulation in a storage buffer, then GPU point rendering. |
| Resource integration | OF FBO/shader compatibility gaps remain. | IOSurface bridges a WebGPU render target to `ofTexture`. |
| OpenGL independence | Intended renderer direction, incomplete for VOSC. | No: bridge requires a current CGL context and a GL rectangle texture. |
| Platform scope | Proposed macOS/iOS integration. | Currently macOS only. |

Sources: [PR](https://github.com/openframeworks/openFrameworks/pull/8577),
[addon interface](https://github.com/ofWorks/ofxDawn/blob/546aba0c6206b32dbbfd9a19fd3ec13f2086fa91/src/ofxDawn.h),
[particle implementation](https://github.com/ofWorks/ofxDawn/blob/546aba0c6206b32dbbfd9a19fd3ec13f2086fa91/src/ofxDawnParticles.cpp),
[texture bridge](https://github.com/ofWorks/ofxDawn/blob/546aba0c6206b32dbbfd9a19fd3ec13f2086fa91/src/ofxDawnSharedTexture.mm).

The addon is an incremental extension, not a replacement renderer. New GPU code
uses WGSL; it does not make existing VOSC GLSL run through WebGPU automatically.
Its current example renders 200,000 particles by default; that is an example
configuration, not a verified VOSC performance result.

### Addon integration and maturity limits

- **Zero-copy does not mean synchronization-free.** The shared IOSurface avoids
  CPU pixel readback, but each particle update calls `waitForGPU()`, which spins
  `ProcessEvents()` until submitted work completes. This can reduce CPU/GPU overlap.
  [Wait implementation](https://github.com/ofWorks/ofxDawn/blob/546aba0c6206b32dbbfd9a19fd3ec13f2086fa91/src/ofxDawn.cpp#L113)
- The current bridge uses BGRA8 and `GL_TEXTURE_RECTANGLE_ARB`. Texture-coordinate,
  alpha, format, and sampling compatibility with VOSC must be checked.
- Resize handling, configurable HDR formats, removing the per-frame wait, and
  instanced particle sprites are documented roadmap items, not delivered behavior.
  [Addon README](https://github.com/ofWorks/ofxDawn/blob/546aba0c6206b32dbbfd9a19fd3ec13f2086fa91/README.md)
- The README describes bundled Dawn headers/static libraries, but the inspected
  tracked tree contains no `libs/` directory. No releases or CI workflows were
  listed. Provisioning a compatible Dawn build remains unresolved by this research.
- The addon configuration includes both ofWorks `macos` and standard OF `osx`
  sections. This does not establish compatibility with VOSC's OF 0.11.1 setup.
  [Addon configuration](https://github.com/ofWorks/ofxDawn/blob/546aba0c6206b32dbbfd9a19fd3ec13f2086fa91/addon_config.mk)

## Suggested next investigations and acceptance evidence

These are proposed follow-ups, not work executed or authorized by this document.

1. Keep OpenGL as the baseline and validate target macOS updates before live use.
2. If compute or a new simulation source is needed, try one isolated ofxDawn
   texture source through VOSC's existing source/layer interfaces.
3. If avoiding Apple's GL implementation is the objective, investigate maintained
   ANGLE-on-Metal separately from the old MetalANGLE fork.
4. Revisit OF Dawn when its shader/FBO/instancing coverage meets VOSC's needs,
   or evaluate it within an explicitly scoped renderer rewrite.

A useful comparison scene should include custom GLSL/hot reload, feedback,
video, layered composition, effects, camera/instancing, and OSC/onset timing.
Check resize/recovery, texture coordinates/formats/alpha, preview readback,
GPU timing, and protected-authoring behavior. Measure equivalent output size,
content, frame pacing, CPU time, GPU time, and synchronization overhead on target
hardware. Record backend/dependency revisions and distinguish clean builds from
legacy/prebuilt-library reuse.

## Research boundaries

- Read-only source, documentation, GitHub metadata, and one local CGL capability
  probe were used. No alternate renderer was installed, built, or adopted.
- No comparative VOSC scene benchmark, sustained installation test, complete
  addon compatibility audit, or physical display acceptance was completed.
- External project maturity/status may change after the dated snapshots above.
- Source presence, author benchmarks, CI success, milestones, and maintainer
  approvals are different evidence categories and should not be substituted for
  one another.
