#pragma once

#include "Source.h"
#include "SketchImpl.h"
#include "ofMain.h"
#include "ShaderData.h"

class Sketch : public Source {
public:
    static map<string, SketchImpl*> sketches;
    void update(ShaderData *shaderData, Config &config);
    void draw(int left, int top, int width, int height) override;
    void choose() override;
    void clear();
    
    ofFbo fbo;
    SketchImpl *impl;
};
