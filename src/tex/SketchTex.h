#pragma once

#include "FboTex.h"
#include "Sketch.h"

class SketchTex : public FboTex {
public:
    static map<string, Sketch*> sketches;
    static bool exists(string path);
    static string random();
    
    SketchTex(string name, const vector<float>& args) : FboTex(name, args) {}
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
