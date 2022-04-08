#pragma once

#include "Gen.h"

class FBOGen : public Gen {
public:
    FBOGen(string path) : Gen(path) {}
    void clear() override;
    
    ofFbo fbo;
};
