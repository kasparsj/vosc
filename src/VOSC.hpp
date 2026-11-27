#pragma once

#include "ofMain.h"
#include "RuntimeContext.hpp"
#include "systems/CommandSystem.hpp"
#include "systems/InputSystem.hpp"
#include "systems/RenderSystem.hpp"
#include "systems/ResourceSystem.hpp"
#include "control/EngineControl.hpp"
#include <functional>
#include <memory>
#include <optional>
#ifdef VOSC_RECOVERY_TESTING
#include <string>
#endif

class VOSC {
public:
    VOSC();
    ~VOSC();

    void setup(std::optional<unsigned int> port, bool draftSamples, const scene::File* scene, CommandSystem::ScenePrograms& programs,
               CommandSystem::SceneResult result);
    void update();
    void draw();
    void exit();
    
#if VOSC_OPERATOR_UI
    using ExampleLoadResult = std::function<void(const scene::File* loaded, int frame, const std::string& detail)>;
    bool requestExampleLoad(std::filesystem::path manifest, scene::Inputs inputs, ExampleLoadResult result);
    const auto& operatorLayers() const { return renderSystem().getLayers(); }
    std::optional<glm::vec3> operatorCameraPosition() const;
    void applyOperatorTextureFlag(const std::function<std::shared_ptr<Texture>()>& resolve,
                                  bool Texture::*flag, bool value);
#endif
    void keyPressed(int key);
    void windowResized(int w, int h);

private:
    void setupControl();
#ifdef VOSC_RECOVERY_TESTING
    void recoveryTestUpdate(bool beforeDraw = false);
    void recoveryTestRecord(const ofJson& event);
    std::string recoveryTestDirectory;
    std::string recoveryTestCommandPath;
    std::string recoveryTestEventsPath;
#endif
    struct SceneState;
    ResourceSystem& resourceSystem();
    RuntimeContext& runtime();
    RenderSystem& renderSystem();
    const RenderSystem& renderSystem() const;
    CommandSystem& commandSystem();
    void attachLiveCommands();

    InputSystem inputSystem;
    std::unique_ptr<SceneState> activeScene;
    control::EngineControl engineControl;
#if VOSC_OPERATOR_UI
    struct ExampleLoad;
    std::unique_ptr<ExampleLoad> exampleLoad;
    ofStyle applicationCompositionStyle;
    void prepareExample();
    bool composeExample();
    void finishExample(int frame, std::string detail);
    bool exampleLoadBusy();
#ifdef VOSC_RECOVERY_TESTING
    bool failNextExampleComposition = false;
    std::optional<double> lastExampleDrawStart;
    std::optional<int> lastExampleOutcomeFrame;
#endif
#endif
    bool initialized = false;
    int settledWindowWidth = 0;
    int settledWindowHeight = 0;
};
