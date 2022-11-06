#pragma once

#include "Tex.h"

class FBOTex : public Tex {
public:
    FBOTex(string path) : Tex(path) {}
    void clear() override;
    ofPixels& getPixels() override {
        fbo.readToPixels(pixels);
        return pixels;
    }
    
    ofFbo fbo;
    ofPixels pixels;
};
