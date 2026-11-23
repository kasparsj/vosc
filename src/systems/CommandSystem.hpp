#pragma once

#include "../RuntimeContext.hpp"
#include "../scene/SceneFile.hpp"
#include <functional>
#include "../osc/CommandParser.hpp"
#include "../osc/CommandRouter.hpp"
#include "InputSystem.hpp"
#include "RenderSystem.hpp"
#include <deque>
#include <optional>
#include <set>
#include <map>

class CommandSystem {
public:
    CommandSystem(RuntimeContext& runtime, RenderSystem& renderSystem, InputSystem& inputSystem)
        : runtime(runtime)
        , renderSystem(renderSystem)
        , inputSystem(inputSystem) {}

    using ScenePrograms = std::map<size_t, Shader::PreparedDirectSource>;
    using SceneResult = std::function<void(int frame, const std::string& detail)>;
    static void validateStartupScene(const scene::File& scene, const ScenePrograms* programs = nullptr);
    static ScenePrograms prepareStartupScene(const scene::File& scene);
    void applyStartupScene(const scene::File& scene, ScenePrograms& programs, SceneResult result,
                           std::optional<std::pair<int, int>> compositionBacking = std::nullopt);
    void discardSceneSetup();
    bool startupScenePending() const { return static_cast<bool>(startupSceneResult); }

    void setup();
    void update();
    bool didOpenGateThisUpdate() const { return gateOpened; }
    bool isGateEnabled() const { return waitOnset >= 1; }
    int onsetGateValue() const { return waitOnset; }
#ifdef VOSC_RECOVERY_TESTING
    static void traceStartupBoundary(const char* event);
    bool recoveryTestSceneReplayEnded() const { return sceneReplay && sceneReplay->ended; }
#endif
    bool validateSceneRecipeCommands(const ofJson& commands, const ofJson& assets, std::string& reason) const;
    bool replayActive() const { return sceneReplay.has_value(); }
    size_t pendingCount() const { return messageQueue.size(); }
    void afterComposedDraw();
    bool handleReplayWindowResized(int width, int height);
    std::function<void()> onLegacyControl;
    std::function<void()> onLegacySample;
    std::function<void(bool inputSample)> onBeforeLegacyControl;
    std::function<bool()> canBeginSceneReplay;

private:
    struct SceneReplay;
    bool isQueuedCommand(osc::CommandType type) const;
    bool checkOnset() const;
    void parseMessage(const ofxOscMessage& m);
    void processQueue();
    bool hasCommandRegistry(const ofxOscMessage& message) const;
    bool routeLegacyCommand(const osc::Command& command);
    void handleMidi(const osc::Command& command);
    void routeTargetedResource(const osc::Command& command);
    void invalidCommand(const ofxOscMessage& m) const;
    void invalidCommand(const osc::ParseError& error) const;
    bool handleSceneEnvelope(const ofxOscMessage& message);
    void failSceneReplay(const std::string& detail);
    void sendSceneReply(const std::string& host, int port, const std::string& token,
                        const std::string& status, int frame, const std::string& detail) const;
    bool routeSceneCommand(const ofxOscMessage& wrapped, Shader::PreparedDirectSource* program = nullptr);
    static bool validateSceneCommand(const ofxOscMessage& wrapped, SceneReplay& state,
                                     const Shader::Uniforms* uniforms = nullptr);
    const Shader::Uniforms* liveSceneUniforms(const ofxOscMessage& wrapped) const;
    void completeSceneReplay(const std::string& status, int frame, const std::string& detail);
    void resetScene(int width, int height);
    bool matchesCompositionSize(int width, int height) const;
    void applyNativeCommand(const scene::NativeCommand& command);
    void afterNativeDraw();
    void finishNativeStartup(int frame, const std::string& detail);
    void verifyStartupImage(const ofxOscMessage& message, const std::string& path);
    static bool validateFeedbackSettings(const SceneReplay& state, std::string& reason);
    bool finishFeedbackSetup(const SceneReplay& state, std::string& reason) const;
    bool finishFeedbackTexture(int layer, std::string& reason) const;

    struct NativeStartup {
        const scene::File& file;
        size_t nextCommand = 0;
        uint64_t diagnosticStart = 0;
        std::vector<std::shared_ptr<Texture>> textures;
        std::shared_ptr<Texture> media;
#ifdef VOSC_RECOVERY_TESTING
        bool suppressedMediaFrame = false;
#endif
    };
    std::optional<NativeStartup> nativeStartup;

    struct SceneReplay {
        std::string token;
        std::string host;
        int sourcePort = 0;
        int replyPort = 0;
        int width = 0;
        int height = 0;
        int nextSequence = 1;
        int received = 0;
        int expected = -1;
        int finalGate = -1;
        bool dependenciesClosed = false;
        bool resourcesStarted = false;
        double deadline = 0;
        bool ended = false;
        uint64_t diagnosticStart = 0;
        uint64_t configurationSerial = 0;
        int layerCount = -1;
        std::set<std::string> inputs;
        std::set<std::string> sharedVariables;
        std::set<std::string> sharedExpressions;
        std::set<std::string> referencedShared;
        std::map<std::string, std::set<std::string>> sharedDependencies;
        std::set<int> shaders;
        std::set<int> defaultTextures;
        std::set<std::pair<int, std::string>> namedTextures;
        std::map<int, std::set<std::string>> shaderBindings;
        int components = 0;
        int feedbackLayer = -1;
        std::set<std::string> feedbackSettings;
        std::set<std::string> feedbackControls;
    };

    RuntimeContext& runtime;
    RenderSystem& renderSystem;
    InputSystem& inputSystem;
    std::deque<osc::Command> messageQueue;
    osc::CommandParser commandParser;
    osc::CommandRouter commandRouter;
    int waitOnset = -1;
    bool forceOnset = false;
    bool gateOpened = false;
    std::optional<SceneReplay> sceneReplay;
    SceneResult startupSceneResult;
    std::optional<std::pair<int, int>> startupCompositionBacking;
public:
    std::function<uint64_t()> currentConfigurationSerial;
};
