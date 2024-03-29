#pragma once

#include "FboTex.h"
#include "Sketch.h"

class SketchTex : public FboTex {
public:
    static map<string, shared_ptr<Sketch>> sketches;
    static bool exists(string path);
    static string random();
    
    SketchTex(string name, const vector<float>& args) : FboTex(name, args) {}
    ~SketchTex() {}
    void update(TexData& data) override;
    void choose() override;
    void reset() override;
    
    shared_ptr<Sketch> impl;
};
