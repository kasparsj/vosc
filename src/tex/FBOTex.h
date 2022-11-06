#pragma once

#include "Tex.h"

class FBOTex : public Tex {
public:
    FBOTex(string path, const vector<float>& args) : Tex(path, args) {}
    void clear() override;
    ofPixels& getPixels() override {
        fbo.readToPixels(pixels);
        return pixels;
    }
    
    ofFbo fbo;
    ofPixels pixels;
};
