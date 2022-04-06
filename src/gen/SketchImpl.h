#pragma once

#include "ofMain.h"
#include "Config.h"
#include "LayerData.h"

class SketchImpl {
public:
    virtual void init() {
        initialized = true;
    }
    virtual void draw(LayerData *shaderData, Config &config) = 0;
    
    bool initialized;
    ofFloatColor defaultColor = ofFloatColor(1.0);
};
