#pragma once

#include "ofxAutoReloadedShader.h"

class LayerShader {
    
public:
    void load(string path);
    void begin();
    void end();
    
    ofxAutoReloadedShader shader;
};
