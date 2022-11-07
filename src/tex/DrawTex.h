#pragma once

#include "FboTex.h"

class DrawTex : public FboTex {
public:
    static vector<string> draws;
    static string random();
    
    DrawTex(string path, const vector<float>& args) : FboTex(path, args) {}
    ~DrawTex() {}
    void update(Layer* layer, Texture* tex) override;
    void choose() override;

};
