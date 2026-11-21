#include "VOSC.hpp"
#include "Config.h"
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
#endif

VOSC::VOSC()
    : resourceSystem()
    , runtime()
    , inputSystem()
    , renderSystem()
    , commandSystem(runtime, renderSystem, inputSystem)
    , engineControl(renderSystem, commandSystem, runtime) {}

void VOSC::setup(unsigned int port) {
    inputSystem.setup(port);
    resourceSystem.setup();
    runtime.camera.setup();
    renderSystem.setup(INITIAL_LAYERS);
    commandSystem.setup();
    commandSystem.onBeforeLegacyControl = [this](bool inputSample) { engineControl.beforeLegacyControl(inputSample); };
    commandSystem.onLegacyControl = [this]() { engineControl.noteExternalChange(); };
    commandSystem.onLegacySample = [this]() { engineControl.noteExternalSample(); };
    commandSystem.canBeginSceneReplay = [this]() { return engineControl.canBeginSceneReplay(); };
    commandSystem.currentConfigurationSerial = [this]() { return engineControl.configurationSerial(); };
    renderSystem.windowResized(ofGetWidth(), ofGetHeight());
    settledWindowWidth = ofGetWidth();
    settledWindowHeight = ofGetHeight();
    engineControl.setup();
#ifdef VOSC_RECOVERY_TESTING
    if (const char* directory = std::getenv("VOSC_RECOVERY_TEST_DIR")) {
        recoveryTestDirectory = directory;
        recoveryTestCommandPath = recoveryTestDirectory + "/command.json";
        recoveryTestEventsPath = recoveryTestDirectory + "/events.jsonl";
        recoveryTestRecord({{"event", "started"}, {"snapshot", engineControl.recoveryTestSnapshot()}});
    }
#endif

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

void VOSC::update() {
#ifdef VOSC_RECOVERY_TESTING
    recoveryTestUpdate();
#endif
    runtime.camera.preUpdate();
    commandSystem.update();
    engineControl.update();
    runtime.inputs.update();
    while (runtime.tidal->notes.size() > MAX_NOTES) {
        runtime.tidal->notes.erase(runtime.tidal->notes.begin());
    }
    engineControl.beforeUpdate();
    resourceSystem.update(runtime.tidal->notes);
    renderSystem.update(runtime.tidal->notes);
    engineControl.afterUpdate();
    runtime.camera.update();
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
    engineControl.beforeDraw();
    renderSystem.draw(runtime.camera, showDebug, [this]() {
        commandSystem.afterComposedDraw();
        engineControl.afterComposedDraw();
        engineControl.afterDraw();
    });
}

void VOSC::mousePressed(int x, int y, int button) {
    engineControl.interruptTrial();
    engineControl.noteExternalChange();
    renderSystem.mousePressed(x, y, button, showDebug);
}

void VOSC::mouseReleased(int x, int y, int button) {
    engineControl.interruptTrial();
    engineControl.noteExternalChange();
    renderSystem.mouseReleased(x, y, button, showDebug);
}

void VOSC::keyPressed(int key) {
    engineControl.interruptTrial();
    engineControl.noteExternalChange();
    switch (key) {
        case 'f':
            ofToggleFullscreen();
            break;
        case OF_KEY_TAB:
            showDebug = !showDebug;
            break;
        default:
            renderSystem.keyPressed(key, showDebug);
            break;
    }
}

void VOSC::windowResized(int w, int h) {
#ifdef VOSC_RECOVERY_TESTING
    if (!recoveryTestDirectory.empty()) {
        recoveryTestRecord({{"event", "native_window_callback"}, {"source", "glfw_native_callback"},
            {"width", w}, {"height", h}, {"suppressed", recoveryTestSuppressResizeCallbacks != 0},
            {"snapshot", engineControl.recoveryTestSnapshot()}});
        if (recoveryTestSuppressResizeCallbacks != 0) {
            if (recoveryTestSuppressResizeCallbacks > 0) --recoveryTestSuppressResizeCallbacks;
            return;
        }
    }
#endif
    if (commandSystem.handleReplayWindowResized(w, h)) {
        settledWindowWidth = w;
        settledWindowHeight = h;
        return;
    }
    if (engineControl.handleWindowResized(w, h)) {
        settledWindowWidth = w;
        settledWindowHeight = h;
        return;
    }
    if (w == settledWindowWidth && h == settledWindowHeight &&
        ofGetWidth() == w && ofGetHeight() == h) {
        return;
    }
    engineControl.interruptTrial();
    renderSystem.windowResized(w, h);
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
        !commandSystem.recoveryTestSceneReplayEnded()) return;
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
    } else if (operation == "suppress_resize_callbacks") {
        recoveryTestSuppressResizeCallbacks = command.value("count", 1);
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation},
            {"count", recoveryTestSuppressResizeCallbacks}, {"snapshot", snapshot}});
    } else if (operation == "release_resize_callbacks") {
        recoveryTestSuppressResizeCallbacks = 0;
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation},
            {"snapshot", snapshot}});
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
    } else if (operation == "inject_resize_callback") {
        auto result = engineControl.recoveryTestInjectResizeCallback(
            command.at("expected_width").get<int>(), command.at("expected_height").get<int>(),
            command.at("width").get<int>(), command.at("height").get<int>(),
            command.value("synchronous", false));
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation},
            {"result", result}});
    } else if (operation == "arm_setter_mismatch") {
        engineControl.recoveryTestArmSynchronousResizeMismatch(
            command.at("width").get<int>(), command.at("height").get<int>());
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation},
            {"source", "deterministic_requestInternalResize_injection"}, {"snapshot", snapshot}});
    } else if (operation == "inspect_programs") {
        std::vector<unsigned> programs;
        for (const auto& value : command.at("programs")) programs.push_back(value.get<unsigned>());
        recoveryTestRecord({{"event", "command_complete"}, {"id", id}, {"operation", operation},
            {"result", engineControl.recoveryTestInspectPrograms(programs)}});
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
    inputSystem.shutdown();
#ifdef VOSC_RECOVERY_TESTING
    recoveryTestRecord({{"event", "input_stopped"}});
#endif
    engineControl.shutdown();
    renderSystem.shutdown();
    resourceSystem.shutdown();
    runtime.tidal.reset();
#ifdef VOSC_RECOVERY_TESTING
    recoveryTestRecord({{"event", "downstream_stopped"}});
#endif
#if USE_OFX_HPVPLAYER
    HPV::DestroyHPVEngine();
#endif
}
