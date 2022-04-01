#pragma once

#include "Generator.h"
#include "SketchImpl.h"
#include "ofMain.h"
#include "VisualData.h"

class Sketch : public Generator {
public:
    static map<string, SketchImpl*> sketches;
    void update(VisualData *data, Config &config);
    void draw(int left, int top, int width, int height) override;
    void choose() override;
    void clear();
    void reset();
    
    ofFbo fbo;
    SketchImpl *impl;
};
