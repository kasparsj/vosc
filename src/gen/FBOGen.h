#pragma once

#include "Gen.h"

class FBOGen : public Gen {
public:
    FBOGen(string path) : Gen(path) {}
    void clear() override;
    ofPixels& getPixels() override {
        fbo.readToPixels(pixels);
        return pixels;
    }
    
    ofFbo fbo;
    ofPixels pixels;
};
