#pragma once

#include "ofMain.h"
#include "Layer.h"

class SketchImpl {
public:
    virtual void init() {
        initialized = true;
    }
    virtual void draw(Layer *layer) = 0;
    
    bool initialized;
    ofFloatColor defaultColor = ofFloatColor(1.0);
};
