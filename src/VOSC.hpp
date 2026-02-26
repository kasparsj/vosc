#pragma once

#include "ofMain.h"
#include "RuntimeContext.hpp"
#include "systems/CommandSystem.hpp"
#include "systems/InputSystem.hpp"
#include "systems/RenderSystem.hpp"
#include "systems/ResourceSystem.hpp"
#include <memory>

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
    ResourceSystem resourceSystem;
    RuntimeContext runtime;
    InputSystem inputSystem;
    RenderSystem renderSystem;
    CommandSystem commandSystem;
};
