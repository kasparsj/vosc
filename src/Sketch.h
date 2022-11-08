#pragma once

#include "ofMain.h"
#include "Texture.h"

class Sketch {
public:
    virtual void init() {
        initialized = true;
    }
    virtual void draw(TexData& data) = 0;
    
    bool initialized;
    ofFloatColor defaultColor = ofFloatColor(1.0);
};
