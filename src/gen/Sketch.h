#pragma once

#include "FBOGen.h"
#include "SketchImpl.h"

class Sketch : public FBOGen {
public:
    static map<string, SketchImpl*> sketches;
    static bool exists(string path);
    static string random();
    
    Sketch(string name) : FBOGen(name) {}
    ~Sketch() {
        if (impl != NULL) {
            delete impl;
        }
    }
    void update(Layer *layer) override;
    void draw(const glm::vec3 &pos, const glm::vec3 &size) override;
    void choose() override;
    void reset() override;
    
    SketchImpl *impl = NULL;
};
