#pragma once

#include "ofMain.h"
#include "RuntimeContext.hpp"
#include "systems/CommandSystem.hpp"
#include "systems/InputSystem.hpp"
#include "systems/RenderSystem.hpp"
#include "systems/ResourceSystem.hpp"
#include "control/EngineControl.hpp"
#include <memory>
#ifdef VOSC_RECOVERY_TESTING
#include <string>
#endif

class VOSC {
public:
    VOSC();

    void setup(unsigned int port);
    void update();
    void draw();
    void exit();
    
    void mousePressed(int x, int y, int button);
    void mouseReleased(int x, int y, int button);
    void keyPressed(int key);
    void windowResized(int w, int h);
    
    bool showDebug = false;

private:
#ifdef VOSC_RECOVERY_TESTING
    void recoveryTestUpdate(bool beforeDraw = false);
    void recoveryTestRecord(const ofJson& event);
    std::string recoveryTestDirectory;
    std::string recoveryTestCommandPath;
    std::string recoveryTestEventsPath;
    int recoveryTestSuppressResizeCallbacks = 0;
#endif
    ResourceSystem resourceSystem;
    RuntimeContext runtime;
    InputSystem inputSystem;
    RenderSystem renderSystem;
    CommandSystem commandSystem;
    control::EngineControl engineControl;
    int settledWindowWidth = 0;
    int settledWindowHeight = 0;
};
