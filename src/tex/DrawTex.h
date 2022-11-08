#pragma once

#include "FboTex.h"

class DrawTex : public FboTex {
public:
    static vector<string> draws;
    static string random();
    
    DrawTex(string path, const vector<float>& args) : FboTex(path, args) {}
    ~DrawTex() {}
    void update(TexData& data) override;
    void choose() override;

};
