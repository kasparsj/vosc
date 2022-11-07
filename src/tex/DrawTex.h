#pragma once

#include "FBOTex1.h"

class DrawTex : public FBOTex1 {
public:
    static vector<string> draws;
    static string random();
    
    DrawTex(string path, const vector<float>& args) : FBOTex1(path, args) {}
    ~DrawTex() {}
    void update(Layer* layer, Texture* tex) override;
    void choose() override;

};
