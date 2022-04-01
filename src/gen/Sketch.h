#pragma once

#include "FBOGen.h"
#include "SketchImpl.h"
#include "ofMain.h"
#include "VisualData.h"

class Sketch : public FBOGen {
public:
    static map<string, SketchImpl*> sketches;
    void update(VisualData *data, Config &config);
    void draw(int left, int top, int width, int height) override;
    void choose() override;
    void reset();
    
    SketchImpl *impl;
};
