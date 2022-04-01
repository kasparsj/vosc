#pragma once

#include "ofMain.h"
#include "Config.h"
#include "VisualData.h"

class SketchImpl {
public:
    virtual void init() {
        initialized = true;
    }
    virtual void draw(VisualData *shaderData, Config &config) = 0;
    
    bool initialized;
};
