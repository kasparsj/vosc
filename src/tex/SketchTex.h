#pragma once

#include "FBOTex.h"
#include "Sketch.h"

class SketchTex : public FBOTex {
public:
    static map<string, Sketch*> sketches;
    static bool exists(string path);
    static string random();
    
    SketchTex(string name, const vector<float>& args) : FBOTex(name, args) {}
    ~SketchTex() {
        if (impl != NULL) {
            delete impl;
        }
    }
    void update(Layer *layer) override;
    void draw(const glm::vec3 &pos, const glm::vec3 &size) override;
    void choose() override;
    void reset() override;
    
    Sketch *impl = NULL;
};
