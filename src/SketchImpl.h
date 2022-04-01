#pragma once

#include "ofMain.h"
#include "Config.h"
#include "ShaderData.h"

class SketchImpl {
public:
    virtual void init() {
        initialized = true;
    }
    virtual void draw(ShaderData *shaderData, Config &config) = 0;
    
    bool initialized;
};
