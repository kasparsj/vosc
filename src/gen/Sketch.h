#pragma once

#include "FBOGen.h"
#include "SketchImpl.h"

class Sketch : public FBOGen {
public:
    static map<string, SketchImpl*> sketches;
    void update(VisualData *data, Config &config) override;
    void draw(int left, int top, int width, int height) override;
    void choose() override;
    void reset();
    
    SketchImpl *impl;
};
