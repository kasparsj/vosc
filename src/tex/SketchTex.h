#pragma once

#include "FBOTex1.h"
#include "Sketch.h"

class SketchTex : public FBOTex1 {
public:
    static map<string, Sketch*> sketches;
    static bool exists(string path);
    static string random();
    
    SketchTex(string name, const vector<float>& args) : FBOTex1(name, args) {}
    ~SketchTex() {
        if (impl != NULL) {
            delete impl;
        }
    }
    void update(Layer *layer, Texture* tex) override;
    void draw(const glm::vec2 &size) override;
    void choose() override;
    void reset() override;
    
    Sketch *impl = NULL;
};
