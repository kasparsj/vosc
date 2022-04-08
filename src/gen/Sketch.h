#pragma once

#include "FBOGen.h"
#include "SketchImpl.h"

class Sketch : public FBOGen {
public:
    static map<string, SketchImpl*> sketches;
    static string random();
    
    Sketch(string name) : FBOGen(name) {}
    ~Sketch() {
        if (impl != NULL) {
            delete impl;
        }
    }
    void update(Layer *layer) override;
    void draw(int left, int top, int width, int height) override;
    void choose() override;
    void reset() override;
    
    SketchImpl *impl = NULL;
};
