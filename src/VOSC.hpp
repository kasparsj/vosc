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

    void setup(unsigned int port, const scene::File* scene, CommandSystem::ScenePrograms& programs,
               CommandSystem::SceneResult result);
    void update();
    void draw();
    void exit();
    
#if VOSC_OPERATOR_UI
    const auto& operatorLayers() const { return renderSystem.getLayers(); }
    std::optional<glm::vec3> operatorCameraPosition() const {
        if (!runtime.camera.isEnabled()) return std::nullopt;
        return runtime.camera.getCamera().getPosition();
    }
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
    ResourceSystem resourceSystem;
    RuntimeContext runtime;
    InputSystem inputSystem;
    RenderSystem renderSystem;
    CommandSystem commandSystem;
    control::EngineControl engineControl;
    bool initialized = false;
    int settledWindowWidth = 0;
    int settledWindowHeight = 0;
};
