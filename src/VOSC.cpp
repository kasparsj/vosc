#include "VOSC.hpp"
#include "Config.h"
#include "light/Lights.h"
#include <stdexcept>
#if VOSC_OPERATOR_UI
#include "Logger.h"
#include "ofAppGLFWWindow.h"
#include "ofRandomEngine.h"
#include "tex/VideoTex.h"
#include "tex/WebcamTex.h"
#if USE_OFX_EXTREME_GPU_VIDEO
#include "tex/GVVideoTex.h"
#endif
#if USE_OFX_HPVPLAYER
#include "tex/HPVideoTex.h"
#endif
#include <GLFW/glfw3.h>
#include <chrono>
#include <utility>
#endif
#ifdef VOSC_RECOVERY_TESTING
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include "tex/ShaderPingPongTex.h"
#endif

#if USE_OFX_HPVPLAYER
#include "ofxHPVPlayer.h"
#endif
#if USE_OFX_ULTRALIGHT
#include "ofxUltralight.h"
#if VOSC_OPERATOR_UI
#include "tex/UltralightTex.h"
#endif
#endif

struct VOSC::SceneState {
    explicit SceneState(InputSystem& input)
        : runtime(*resources.registry().inputs)
        , commands(runtime, render, input) {}

    ~SceneState() {
        ResourceRegistry::Scope context(resources.registry());
        commands.discardSceneSetup();
        runtime.inputs.clear();
        resources.registry().lights->clear();
        render.shutdown();
        resources.shutdown();
    }

    ResourceSystem resources;
    RuntimeContext runtime;
    RenderSystem render;
    CommandSystem commands;
};

#if VOSC_OPERATOR_UI
namespace {
struct WindowTarget {
    GLFWwindow* window = nullptr;
    int rendererWidth = 0, rendererHeight = 0;
    int windowWidth = 0, windowHeight = 0;
    int pixelWidth = 0, pixelHeight = 0;
    ofWindowMode mode = OF_WINDOW;

    bool operator==(const WindowTarget& other) const {
        return window == other.window && rendererWidth == other.rendererWidth &&
            rendererHeight == other.rendererHeight && windowWidth == other.windowWidth &&
            windowHeight == other.windowHeight && pixelWidth == other.pixelWidth &&
            pixelHeight == other.pixelHeight && mode == other.mode;
    }
};

WindowTarget currentWindowTarget() {
    WindowTarget target;
    auto* window = dynamic_cast<ofAppGLFWWindow*>(ofGetWindowPtr());
    if (!window || !window->getGLFWWindow()) return target;
    target.window = window->getGLFWWindow();
    target.rendererWidth = ofGetWidth();
    target.rendererHeight = ofGetHeight();
    target.mode = window->getWindowMode();
    glfwGetWindowSize(target.window, &target.windowWidth, &target.windowHeight);
    glfwGetFramebufferSize(target.window, &target.pixelWidth, &target.pixelHeight);
    return target;
}

class PrivateDraw {
public:
    PrivateDraw() : arbTextures(ofGetUsingArbTex()) {
        ofPushView();
        ofPushStyle();
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        depth = glIsEnabled(GL_DEPTH_TEST);
        cull = glIsEnabled(GL_CULL_FACE);
    }

    ~PrivateDraw() {
        ofPopStyle();
        ofPopView();
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        glDepthMask(depthMask);
        restore(GL_SCISSOR_TEST, scissor);
        restore(GL_DEPTH_TEST, depth);
        restore(GL_CULL_FACE, cull);
        if (arbTextures) ofEnableArbTex();
        else ofDisableArbTex();
    }

private:
    static void restore(GLenum capability, bool enabled) {
        if (enabled) glEnable(capability);
        else glDisable(capability);
    }

    const bool arbTextures;
    GLint readFramebuffer = 0, drawFramebuffer = 0, renderbuffer = 0;
    GLint viewport[4] = {}, scissorBox[4] = {};
    GLboolean colorMask[4] = {}, depthMask = GL_TRUE;
    GLboolean scissor = GL_FALSE, depth = GL_FALSE, cull = GL_FALSE;
};

class CandidateRandom {
public:
    explicit CandidateRandom(std::mt19937& candidate) : candidate(candidate) {
        std::swap(of::random::gen(), candidate);
    }
    ~CandidateRandom() { std::swap(of::random::gen(), candidate); }

private:
    std::mt19937& candidate;
};

using CompositionLights = std::vector<std::weak_ptr<ofLight::Data>>;

class CandidateLights {
public:
    explicit CandidateLights(CompositionLights& candidate) : candidate(candidate) {
        ofLightsData().swap(candidate);
    }
    ~CandidateLights() { ofLightsData().swap(candidate); }

private:
    CompositionLights& candidate;
};

class BoundFramebuffer {
public:
    explicit BoundFramebuffer(const ofFbo& target) : target(target) { target.bind(); }
    ~BoundFramebuffer() { target.unbind(); }

private:
    const ofFbo& target;
};

void clearExampleGlErrors() {
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {}
}

void checkExampleGlError(const char* operation) {
    if (const auto error = glGetError(); error != GL_NO_ERROR)
        throw std::runtime_error("OpenGL error " + std::to_string(error) + " while " + operation + ".");
}

void allocateComposition(ofFbo& framebuffer, const WindowTarget& target, GLint samples) {
    GLint textureLimit = 0, renderbufferLimit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &textureLimit);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &renderbufferLimit);
    const auto sideLimit = std::min(textureLimit, renderbufferLimit);
    const uint64_t pixels = uint64_t(target.pixelWidth) * target.pixelHeight;
    constexpr uint64_t maxCompositionBytes = 256u * 1024u * 1024u;
    const auto bytesPerPixel = uint64_t(samples > 0 ? samples * 8 + 4 : 8);
    if (target.pixelWidth > sideLimit || target.pixelHeight > sideLimit ||
        pixels > maxCompositionBytes / bytesPerPixel)
        throw std::runtime_error("The current drawable exceeds the example composition budget.");
    ofFbo::Settings settings;
    settings.width = target.pixelWidth;
    settings.height = target.pixelHeight;
    settings.textureTarget = GL_TEXTURE_2D;
    settings.internalformat = GL_RGBA8;
    settings.useDepth = true;
    settings.depthStencilInternalFormat = GL_DEPTH_COMPONENT24;
    settings.numSamples = samples;
    framebuffer.allocate(settings);
    if (!framebuffer.isAllocated())
        throw std::runtime_error("Could not allocate the private example composition target.");
}

std::string mediaDescription(const std::shared_ptr<Texture>& texture) {
    auto* source = texture ? texture->tex.get() : nullptr;
#ifndef TARGET_OSX
    if (dynamic_cast<WebcamTex*>(source)) return "camera " + source->path;
#endif
    if (!VideoTex::supportsInWindowLoad() && dynamic_cast<VideoTex*>(source))
        return "video " + source->path;
#if USE_OFX_EXTREME_GPU_VIDEO
    if (dynamic_cast<GVVideoTex*>(source)) return "GV video " + source->path;
#endif
#if USE_OFX_HPVPLAYER
    if (dynamic_cast<HPVideoTex*>(source)) return "HPV video " + source->path;
#endif
#if USE_OFX_ULTRALIGHT
    if (dynamic_cast<UltralightTex*>(source)) return "HTML " + source->path;
#endif
    return {};
}

void checkCandidateMedia(const scene::File& file) {
    for (const auto& command : file.nativeCommands) {
        if (command.sourceKind == "html" ||
            (command.sourceKind == "video" && !VideoTex::supportsInWindowLoad())
#ifndef TARGET_OSX
            || command.sourceKind == "camera"
#endif
        ) throw std::runtime_error("This build has no qualified in-window loader for " + file.mediaInput + ".");
    }
#ifndef TARGET_OSX
    for (const auto& command : file.commands) {
        if (command.message.getArgAsString(2) == "/input" && command.message.getArgAsInt32(5) > 0)
            throw std::runtime_error("This build has no qualified in-window audio device loader.");
    }
#endif
}

#ifdef VOSC_RECOVERY_TESTING
double exampleClockMilliseconds() {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

ofJson exampleFirstFramePixels(const WindowTarget& target) {
    ofJson result = ofJson::array();
    for (const auto& position : {glm::ivec2(target.pixelWidth / 8, target.pixelHeight / 8),
                                 glm::ivec2(target.pixelWidth / 2, target.pixelHeight / 2),
                                 glm::ivec2(target.pixelWidth * 7 / 8, target.pixelHeight * 7 / 8)}) {
        std::array<GLubyte, 4> pixel{};
        glReadPixels(position.x, target.pixelHeight - 1 - position.y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        result.push_back({{"x", position.x}, {"y", position.y}, {"rgba", pixel}});
    }
    return result;
}
#endif
}

struct VOSC::ExampleLoad {
    ExampleLoad(std::filesystem::path path, scene::Inputs overrides, ExampleLoadResult completion,
                uint64_t serial, const ofStyle& initialStyle)
        : path(std::move(path)), inputs(std::move(overrides)), completion(std::move(completion))
        , serial(serial), deadline(std::chrono::steady_clock::now() + std::chrono::seconds(12))
        , style(initialStyle) {}

    ~ExampleLoad() { discardCandidate(); }

    void discardCandidate() {
        if (!candidate) return;
        CandidateLights lightScope(lights);
        candidate.reset();
    }

    std::filesystem::path path;
    scene::Inputs inputs;
    ExampleLoadResult completion;
    uint64_t serial;
    std::chrono::steady_clock::time_point deadline;
    ofStyle style;
    WindowTarget target;
    std::optional<scene::File> file;
    std::optional<std::mt19937> random;
    CompositionLights lights;
    std::unique_ptr<SceneState> candidate;
    ofFbo framebuffer;
    std::optional<int> composedFrame;
    std::string resultDetail;
    std::string error;
#ifdef VOSC_RECOVERY_TESTING
    double requestedAtMs = exampleClockMilliseconds();
    double maxDrawGapMs = 0;
    double maxCandidateCpuMs = 0;
    unsigned attempts = 0;

    void noteCandidate(double started) {
        maxCandidateCpuMs = std::max(maxCandidateCpuMs, exampleClockMilliseconds() - started);
        ++attempts;
    }

    ofJson timing() const {
        return {{"request_to_result_ms", exampleClockMilliseconds() - requestedAtMs},
                {"max_draw_start_gap_ms", maxDrawGapMs}, {"max_candidate_cpu_ms", maxCandidateCpuMs},
                {"candidate_attempts", attempts}};
    }
#endif
};
#endif

VOSC::VOSC()
    : activeScene(std::make_unique<SceneState>(inputSystem))
    , engineControl(activeScene->render, activeScene->commands, activeScene->runtime) {}

VOSC::~VOSC() = default;

ResourceSystem& VOSC::resourceSystem() { return activeScene->resources; }
RuntimeContext& VOSC::runtime() { return activeScene->runtime; }
RenderSystem& VOSC::renderSystem() { return activeScene->render; }
const RenderSystem& VOSC::renderSystem() const { return activeScene->render; }
CommandSystem& VOSC::commandSystem() { return activeScene->commands; }

void VOSC::setup(std::optional<unsigned int> port, bool draftSamples, const scene::File* scene, CommandSystem::ScenePrograms& programs,
                 CommandSystem::SceneResult result) {
#if VOSC_OPERATOR_UI
    applicationCompositionStyle = ofGetStyle();
#endif
    resourceSystem().setup();
    initialized = true;
#ifdef VOSC_RECOVERY_TESTING
    if (!scene && std::getenv("VOSC_TEST_FAIL_NO_ARG_SETUP"))
        throw std::runtime_error("forced no-argument setup failure");
    CommandSystem::traceStartupBoundary("receiver");
#endif
    if (port && !inputSystem.setup(*port)) {
        throw std::runtime_error("Could not bind OSC receiver to UDP port " + std::to_string(*port));
    }
    if (draftSamples) {
        const auto samplePort = inputSystem.setupDraftSamples();
        if (!samplePort) throw std::runtime_error("Could not bind draft sample receiver to loopback");
        engineControl.setDraftSamplePort(*samplePort);
    }
    runtime().camera.setup();
    renderSystem().setup(INITIAL_LAYERS);
    commandSystem().setup();
    attachLiveCommands();
    renderSystem().windowResized(ofGetWidth(), ofGetHeight());
    settledWindowWidth = ofGetWidth();
    settledWindowHeight = ofGetHeight();
    if (scene && scene->version == 2) {
        commandSystem().applyStartupScene(*scene, programs, [this, result = std::move(result)](int frame, const std::string& detail) {
            if (frame >= 0) setupControl();
            result(frame, detail);
        });
    } else {
        if (scene) {
            commandSystem().applyStartupScene(*scene, programs, std::move(result));
            if (!commandSystem().startupScenePending()) return;
        }
        setupControl();
    }

#if defined(__arm64__) || defined(__aarch64__)
    ofLog() << "ARM64: Yes";
#else
    ofLog() << "ARM64: No";
#endif
    ofLog() << ("Vendor :" + ofToString(glGetString(GL_VENDOR)));
    ofLog() << ("GPU : " + ofToString(glGetString(GL_RENDERER)));
    ofLog() << ("OpenGL ver. " + ofToString(glGetString(GL_VERSION)));
    ofLog() << ("GLSL ver. " + ofToString(glGetString(GL_SHADING_LANGUAGE_VERSION)));
}

void VOSC::attachLiveCommands() {
    commandSystem().onBeforeLegacyControl = [this](bool inputSample) { engineControl.beforeLegacyControl(inputSample); };
    commandSystem().onLegacyControl = [this]() { engineControl.noteExternalChange(); };
    commandSystem().onLegacySample = [this]() { engineControl.noteExternalSample(); };
    commandSystem().canBeginSceneReplay = [this]() { return engineControl.canBeginSceneReplay(); };
    commandSystem().currentConfigurationSerial = [this]() { return engineControl.configurationSerial(); };
}

void VOSC::setupControl() {
    engineControl.setup();
#ifdef VOSC_RECOVERY_TESTING
    if (const char* directory = std::getenv("VOSC_RECOVERY_TEST_DIR")) {
        recoveryTestDirectory = directory;
        recoveryTestCommandPath = recoveryTestDirectory + "/command.json";
        recoveryTestEventsPath = recoveryTestDirectory + "/events.jsonl";
        recoveryTestRecord({{"event", "started"}, {"snapshot", engineControl.recoveryTestSnapshot()}});
    }
#endif
}

void VOSC::update() {
#ifdef VOSC_RECOVERY_TESTING
    recoveryTestUpdate();
#endif
    runtime().camera.preUpdate();
    commandSystem().update();
    if (!commandSystem().startupScenePending()) engineControl.update();
    runtime().inputs.update();
    while (runtime().tidal->notes.size() > MAX_NOTES) {
        runtime().tidal->notes.erase(runtime().tidal->notes.begin());
    }
    engineControl.beforeUpdate();
    resourceSystem().update(runtime().tidal->notes);
    renderSystem().update(runtime().tidal->notes);
    engineControl.afterUpdate();
    runtime().camera.update();
#if USE_OFX_HPVPLAYER
    HPV::Update();
#endif
#if USE_OFX_ULTRALIGHT
    ofxUltralight::update();
#endif
}

void VOSC::draw() {
#ifdef VOSC_RECOVERY_TESTING
    // Commands that target rolling_back must run after EngineControl::update
    // establishes that phase and before afterDraw can retire it.
    recoveryTestUpdate(true);
#endif
#if VOSC_OPERATOR_UI
#ifdef VOSC_RECOVERY_TESTING
    const auto drawStart = exampleClockMilliseconds();
    if (lastExampleDrawStart) {
        const auto gap = drawStart - *lastExampleDrawStart;
        if (exampleLoad) exampleLoad->maxDrawGapMs = std::max(exampleLoad->maxDrawGapMs, gap);
        if (lastExampleOutcomeFrame) {
            recoveryTestRecord({{"event", "example_post_draw_timing"}, {"outcome_frame", *lastExampleOutcomeFrame},
                                {"frame", ofGetFrameNum()}, {"after_result_draw_start_gap_ms", gap}});
            lastExampleOutcomeFrame.reset();
        }
    }
    lastExampleDrawStart = drawStart;
#endif
    if (exampleLoad && composeExample()) return;
#endif
    engineControl.beforeDraw();
    renderSystem().draw(runtime().camera, [this]() {
        commandSystem().afterComposedDraw();
        engineControl.afterComposedDraw();
        engineControl.afterDraw();
    });
}

#if VOSC_OPERATOR_UI
bool VOSC::requestExampleLoad(std::filesystem::path manifest, scene::Inputs inputs, ExampleLoadResult result) {
    if (exampleLoad) return false;
    exampleLoad = std::make_unique<ExampleLoad>(std::move(manifest), std::move(inputs), std::move(result),
                                                engineControl.configurationSerial(), applicationCompositionStyle);
    return true;
}

std::optional<glm::vec3> VOSC::operatorCameraPosition() const {
    if (!activeScene->runtime.camera.isEnabled()) return std::nullopt;
    return activeScene->runtime.camera.getCamera().getPosition();
}

bool VOSC::exampleLoadBusy() {
    return !engineControl.canBeginSceneReplay() || commandSystem().pendingCount() != 0 ||
        commandSystem().startupScenePending() || commandSystem().replayActive();
}

void VOSC::prepareExample() {
    auto& load = *exampleLoad;
    load.file = scene::load(load.path, load.inputs);
    CommandSystem::validateStartupScene(*load.file);
    checkCandidateMedia(*load.file);

    const auto& registry = resourceSystem().registry();
#ifndef TARGET_OSX
    for (const auto& entry : runtime().inputs.all()) {
        if (entry.second->requestedChannels > 0)
            throw std::runtime_error("The current scene owns audio input " + entry.second->requestedDevice +
                                     ". Release it before loading an example.");
    }
#endif
    const auto checkTexture = [](const std::shared_ptr<Texture>& texture) {
        if (const auto description = mediaDescription(texture); !description.empty())
            throw std::runtime_error("The current scene owns " + description + ". Release it before loading an example.");
    };
    for (const auto& layer : renderSystem().getLayers())
        for (const auto& binding : layer->shader.getTextures()) checkTexture(binding.second);
    for (const auto& entry : registry.textureSharedPool) checkTexture(entry.second);
    for (const auto& layer : registry.textureShaderPool)
        for (const auto& binding : layer.second) checkTexture(binding.second);

    load.target = currentWindowTarget();
    if (!load.target.window || load.target.rendererWidth < 1 || load.target.rendererHeight < 1 ||
        load.target.pixelWidth < 1 || load.target.pixelHeight < 1)
        throw std::runtime_error("The current window is not ready to compose an example.");

    auto programs = CommandSystem::prepareStartupScene(*load.file);
    GLint samples = 0;
    glGetIntegerv(GL_SAMPLES, &samples);
    allocateComposition(load.framebuffer, load.target, samples);
    load.candidate = std::make_unique<SceneState>(inputSystem);
    ResourceRegistry::Scope context(load.candidate->resources.registry());
    load.candidate->resources.setup();
    load.candidate->runtime.camera.setInteractive(false);
    load.candidate->commands.setup();
    load.candidate->commands.applyStartupScene(*load.file, programs,
        [&load](int frame, const std::string& detail) {
            load.composedFrame = frame;
            load.resultDetail = detail;
        }, std::pair(load.target.pixelWidth, load.target.pixelHeight));
}

bool VOSC::composeExample() {
    auto& load = *exampleLoad;
#ifdef VOSC_RECOVERY_TESTING
    const auto attemptStart = exampleClockMilliseconds();
#endif
    try {
        if (exampleLoadBusy() || load.serial != engineControl.configurationSerial())
            throw std::runtime_error("The current scene changed or is busy. Try loading the example again.");
        if (std::chrono::steady_clock::now() >= load.deadline)
            throw std::runtime_error("The example timed out before composing its first frame.");
        // Isolate this attempt from errors left by the active scene.
        clearExampleGlErrors();
        const bool direct = RenderSystem::canDrawDirectToWindow();
        checkExampleGlError("checking the current window");
        if (!load.random) load.random = of::random::gen();
        {
            CandidateRandom random(*load.random);
            PrivateDraw drawState;
            checkExampleGlError("saving the current draw state");
            // ofMaterial queries the OF light slots directly, including their count.
            // Isolate slot allocation as well as the enabled lights.
            CandidateLights lightScope(load.lights);
            Logger::ErrorScope logScope(load.error);
            ofSetStyle(load.style);
            checkExampleGlError("applying the example's composition style");
            if (!load.file) {
                prepareExample();
                checkExampleGlError("preparing example resources");
            }
            if (!(currentWindowTarget() == load.target))
                throw std::runtime_error("The window changed while loading. Try loading the example again.");
            if (load.composedFrame && *load.composedFrame < 0)
                throw std::runtime_error(load.resultDetail);

            auto& candidate = *load.candidate;
            ResourceRegistry::Scope context(candidate.resources.registry());
            candidate.runtime.camera.preUpdate();
            candidate.commands.update();
            candidate.runtime.inputs.update();
            candidate.resources.update(candidate.runtime.tidal->notes);
            candidate.render.update(candidate.runtime.tidal->notes);
            candidate.runtime.camera.update();
            checkExampleGlError("updating example resources");
            BoundFramebuffer bound(load.framebuffer);
            if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("The private example composition target is incomplete.");
            ofClear(0, 0, 0, 255);
            checkExampleGlError("binding the example composition target");
            candidate.render.draw(candidate.runtime.camera,
                [&candidate]() { candidate.commands.afterComposedDraw(); }, direct);
            checkExampleGlError("checking the composed example");
            GLint current = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &current);
            if (GLuint(current) != load.framebuffer.getId() ||
                glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("The example draw did not restore its composition target.");
            load.style = ofGetStyle();
        }
        if (!load.error.empty()) throw std::runtime_error(load.error);
        checkExampleGlError("restoring the current draw state");
        if (!load.composedFrame) {
#ifdef VOSC_RECOVERY_TESTING
            load.noteCandidate(attemptStart);
#endif
            return false;
        }
        if (*load.composedFrame < 0) throw std::runtime_error(load.resultDetail);
#ifdef VOSC_RECOVERY_TESTING
        recoveryTestRecord({{"event", "example_candidate_composed"}, {"frame", *load.composedFrame},
                            {"title", load.file->title}});
        if (std::exchange(failNextExampleComposition, false))
            throw std::runtime_error("Injected example composition failure");
#endif
        scene::verifyManifest(*load.file, load.path);
        for (const auto& command : load.file->commands) {
            if (!command.asset.empty() && !scene::assetMatches(command))
                throw std::runtime_error("Example asset changed while loading: " + command.asset.filename().string());
        }
        if (exampleLoadBusy() || load.serial != engineControl.configurationSerial() ||
            !(currentWindowTarget() == load.target))
            throw std::runtime_error("The current scene or window changed while loading. Try again.");

        {
            // Match the native sample count and copy without blending the composed alpha a second time.
            PrivateDraw drawState;
            if (std::chrono::steady_clock::now() >= load.deadline)
                throw std::runtime_error("The example timed out before composing its first frame.");
            glDisable(GL_SCISSOR_TEST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, load.framebuffer.getId());
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glBlitFramebuffer(0, 0, load.target.pixelWidth, load.target.pixelHeight,
                              0, 0, load.target.pixelWidth, load.target.pixelHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            if (glGetError() != GL_NO_ERROR)
                throw std::runtime_error("Could not present the composed example in the current window.");
        }
    } catch (const std::exception& error) {
#ifdef VOSC_RECOVERY_TESTING
        load.noteCandidate(attemptStart);
#endif
        finishExample(-1, error.what());
        return false;
    }
#ifdef VOSC_RECOVERY_TESTING
    load.noteCandidate(attemptStart);
#endif

    activeScene->runtime.camera.setInteractive(false);
    auto retired = std::exchange(activeScene, std::move(load.candidate));
    ofLightsData().swap(load.lights);
    resourceSystem().registry().activate();
    of::random::gen() = std::move(*load.random);
    ofSetStyle(load.style);
    activeScene->runtime.camera.setInteractive(true);
    engineControl.bindScene(activeScene->render, activeScene->commands, activeScene->runtime);
    attachLiveCommands();
    engineControl.beforeDraw();
    engineControl.afterComposedDraw();
    engineControl.afterDraw();
#ifdef VOSC_RECOVERY_TESTING
    if (!recoveryTestDirectory.empty()) {
        recoveryTestRecord({{"event", "example_committed"}, {"frame", *load.composedFrame},
                            {"title", load.file->title}, {"width", load.target.rendererWidth},
                            {"height", load.target.rendererHeight}, {"backing_width", load.target.pixelWidth},
                            {"backing_height", load.target.pixelHeight}, {"timing", load.timing()},
                            {"pixels", exampleFirstFramePixels(load.target)}});
    }
#endif
    {
        CandidateLights retiredLights(load.lights);
        retired.reset();
    }
    finishExample(*load.composedFrame, load.resultDetail);
    return true;
}

void VOSC::finishExample(int frame, std::string detail) {
    auto completed = std::exchange(exampleLoad, {});
    completed->discardCandidate();
#ifdef VOSC_RECOVERY_TESTING
    lastExampleOutcomeFrame = ofGetFrameNum();
    if (frame < 0)
        recoveryTestRecord({{"event", "example_load_failed"}, {"frame", *lastExampleOutcomeFrame},
                            {"detail", detail}, {"timing", completed->timing()}});
#endif
    if (completed->completion)
        completed->completion(frame >= 0 ? &*completed->file : nullptr, frame, detail);
}

void VOSC::applyOperatorTextureFlag(const std::function<std::shared_ptr<Texture>()>& resolve,
                                    bool Texture::*flag, bool value) {
    auto texture = resolve();
    if (!texture || texture.get()->*flag == value) return;
    texture.reset();
    engineControl.interruptTrial();
    // Trial interruption can replace the program or the texture's backing source.
    texture = resolve();
    if (!texture || texture.get()->*flag == value) return;
    texture.get()->*flag = value;
    engineControl.noteExternalChange();
}
#endif

void VOSC::keyPressed(int key) {
    if (key != 'f' && key != 'r' && key != 'u' && key != 'w' && (key < '0' || key > '9')) return;
    engineControl.interruptTrial();
    engineControl.noteExternalChange();
    switch (key) {
        case 'f':
            ofToggleFullscreen();
            break;
        default:
            renderSystem().keyPressed(key);
            break;
    }
}

void VOSC::windowResized(int w, int h) {
#ifdef VOSC_RECOVERY_TESTING
    if (!recoveryTestDirectory.empty()) {
        recoveryTestRecord({{"event", "native_window_callback"}, {"source", "glfw_native_callback"},
            {"width", w}, {"height", h}, {"snapshot", engineControl.recoveryTestSnapshot()}});
    }
#endif
    if (commandSystem().handleReplayWindowResized(w, h)) {
        settledWindowWidth = w;
        settledWindowHeight = h;
        return;
    }
    engineControl.interruptTrial();
    if (w == settledWindowWidth && h == settledWindowHeight &&
        ofGetWidth() == w && ofGetHeight() == h) {
        return;
    }
    renderSystem().windowResized(w, h);
    engineControl.noteExternalChange();
    settledWindowWidth = w;
    settledWindowHeight = h;
}

#ifdef VOSC_RECOVERY_TESTING
void VOSC::recoveryTestRecord(const ofJson& event) {
    if (recoveryTestEventsPath.empty()) return;
    std::ofstream stream(recoveryTestEventsPath, std::ios::app);
    stream << event.dump() << '\n';
}

void VOSC::recoveryTestUpdate(bool beforeDraw) {
    if (recoveryTestCommandPath.empty()) return;
    std::ifstream stream(recoveryTestCommandPath);
    if (!stream) return;
    ofJson command;
    try { stream >> command; } catch (...) { return; }
    const auto stage = command.value("stage", std::string());
    if ((stage == "draw" && !beforeDraw) || (stage == "update" && beforeDraw)) return;
    const std::string id = command.value("id", std::string());
    const std::string operation = command.value("operation", std::string());
    const auto snapshot = engineControl.recoveryTestSnapshot();
    const std::string targetPhase = command.value("target_phase", std::string());
    if (!targetPhase.empty() && snapshot.value("phase", std::string()) != targetPhase) return;
    if (command.count("feedback_updates") &&
        command.at("feedback_updates").get<uint64_t>() != engineControl.recoveryTestFeedbackUpdates()) return;
    if (command.value("require_scene_replay_ended", false) &&
        !commandSystem().recoveryTestSceneReplayEnded()) return;
    std::remove(recoveryTestCommandPath.c_str());
    if (operation == "shutdown") {
        recoveryTestRecord({{"event", "shutdown_requested"}, {"id", id}});
        ofExit();
    } else if (operation == "snapshot") {
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation},
            {"snapshot", snapshot}});
    } else if (operation == "feedback_trial_conflict") {
        auto result = engineControl.recoveryTestConflictFeedbackTrial(command);
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation}, {"result", result}});
    } else if (operation == "feedback_trial_configure" || operation == "feedback_trial_capture") {
        const auto result = operation == "feedback_trial_configure"
            ? engineControl.recoveryTestConfigureFeedbackTrial(command)
            : engineControl.recoveryTestFeedbackCaptureReceipt();
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation}, {"result", result}});
    } else if (operation == "feedback_snapshot" || operation == "feedback_configure") {
        const auto result = operation == "feedback_snapshot" ? ShaderPingPongTex::recoveryTestSnapshot() : ShaderPingPongTex::recoveryTestConfigure(command);
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation}, {"result", result}});
    } else if (operation == "native_resize") {
        const int width = command.at("width").get<int>();
        const int height = command.at("height").get<int>();
        recoveryTestRecord({{"event", "native_window_action"}, {"id", id}, {"operation", operation},
            {"source", "test_driver_ofSetWindowShape"}, {"width", width}, {"height", height},
            {"snapshot", snapshot}});
        ofSetWindowShape(width, height);
    } else if (operation == "dispatch_control" || operation == "feedback_trial_dispatch") {
        if (operation == "feedback_trial_dispatch" && command.value("incomplete_next_destination", false))
            ShaderPingPongTex::recoveryTestConfigure({{"enabled", true}, {"incomplete_next_destination", true}});
        const auto result = engineControl.recoveryTestDispatch(command.at("request"));
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation},
            {"result", result}, {"snapshot", engineControl.recoveryTestSnapshot()}});
    } else if (operation == "external_ui_change") {
        engineControl.noteExternalChange();
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation},
            {"snapshot", engineControl.recoveryTestSnapshot()}});
    } else if (operation == "inspect_programs") {
        std::vector<unsigned> programs;
        for (const auto& value : command.at("programs")) programs.push_back(value.get<unsigned>());
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation},
            {"result", engineControl.recoveryTestInspectPrograms(programs)}});
#if VOSC_OPERATOR_UI
    } else if (operation == "example_load") {
        try {
            const bool accepted = requestExampleLoad(command.at("manifest").get<std::string>(),
                command.value("inputs", scene::Inputs{}),
                [](const scene::File* loaded, int, const std::string&) {
                    if (loaded) ofSetWindowTitle(loaded->title);
                });
            recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation},
                                {"accepted", accepted}});
        } catch (const std::exception& error) {
            recoveryTestRecord({{"event", "command_error"}, {"id", id}, {"operation", operation},
                                {"message", error.what()}});
        }
    } else if (operation == "example_load_fail_next_composition") {
        failNextExampleComposition = true;
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation}});
    } else if (operation == "inspector_texture_probe") {
        const auto name = command.value("texture", std::string(DEFAULT_TEX));
        std::shared_ptr<Texture> texture;
        if (command.value("shared", false)) {
            const auto& pool = resourceSystem().registry().textureSharedPool;
            if (auto found = pool.find(name); found != pool.end()) texture = found->second;
        } else {
            const auto index = command.value("layer", size_t{0});
            const auto& layers = renderSystem().getLayers();
            if (index < layers.size() && layers[index]) {
                const auto& textures = layers[index]->shader.getTextures();
                if (auto found = textures.find(name); found != textures.end()) texture = found->second;
            }
        }
        ofJson result;
        if (texture) {
            if (command.contains("hold")) texture->feedbackSetupPending = command.at("hold").get<bool>();
            result = {{"owner", texture->getId()}, {"source_revision", texture->operatorSourceRevision()},
                      {"static", texture->isStatic}, {"pending", texture->needsUpdate},
                      {"held", texture->feedbackSetupPending}};
        }
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation}, {"result", result}});
#endif
    } else if (operation == "emit_diagnostics") {
        const int count = std::max(0, std::min(command.value("count", 513), 1024));
        for (int index = 0; index < count; ++index) {
            ofLogWarning("recipe-validation") << "owned graphics diagnostic " << index;
        }
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation},
            {"count", count}, {"snapshot", engineControl.recoveryTestSnapshot()}});
    } else {
        recoveryTestRecord({{"event", "command_error"}, {"id", id}, {"operation", operation},
            {"message", "unknown recovery-test operation"}});
    }
}
#endif

void VOSC::exit() {
    if (!initialized) return;
    inputSystem.shutdown();
#ifdef VOSC_RECOVERY_TESTING
    recoveryTestRecord({{"event", "input_stopped"}});
#endif
    engineControl.shutdown();
#if VOSC_OPERATOR_UI
    exampleLoad.reset();
#endif
    activeScene.reset();
    initialized = false;
#ifdef VOSC_RECOVERY_TESTING
    recoveryTestRecord({{"event", "downstream_stopped"}});
#endif
#if USE_OFX_HPVPLAYER
    HPV::DestroyHPVEngine();
#endif
}
